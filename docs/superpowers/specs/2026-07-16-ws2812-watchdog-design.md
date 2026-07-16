# WS2812 灯环与看门狗边界修复设计

## 目标

在现有 STM32F103ZETx + FreeRTOS 工程中加入一个 8 灯 WS2812 兼容 RGB 灯环，并保留已有 UART DMA、Flash、OLED 和看门狗功能。LED 菜单新增 WS2812 配置页，支持固定颜色与动态灯效。

将 UART 历史从 15 个独占 Flash 扇区重构为一个 4 KB 扇区内的追加式日志，对外仍保留最新 15 条记录。

本轮修复清空历史期间 StorageTask 未打卡的看门狗边界。IWDG 在调度器启动前启动的边界，与后续 InitTask 架构重构一并处理。

## 硬件与 CubeMX 约束

- WS2812 DIN 接 `PB0 / TIM3_CH3`。
- TIM3 时钟为 72 MHz，`PSC=0`、`ARR=89`，PWM 周期为 1.25 us。
- TIM3_CH3 使用 DMA1 Channel2，方向 Memory-to-Peripheral，Normal 模式，内存递增、外设地址不递增，内存与外设数据宽度均为 Half Word。
- 灯环使用独立且足够电流的 5 V 电源，与开发板共地；PB0 到 DIN 串联约 330 ohm。优先使用 74HCT125/AHCT125 电平转换。
- CubeMX 生成文件中仅在已有 `USER CODE BEGIN/END` 区块添加手写代码；不移动或手改 CubeMX 生成函数主体。新增外设配置只通过 CubeMX 完成。

## WS2812 驱动

新建 `User/WS2812/ws2812.c` 与 `User/WS2812/ws2812.h`，并在 CMake 中加入该源文件与头文件目录。

驱动维护 8 个 RGB 像素状态。每次刷新按 WS2812 的 GRB 位序编码为 192 个 16 位 PWM 比较值：逻辑 0 写 `25`（约 0.347 us 高电平），逻辑 1 写 `50`（约 0.694 us 高电平）。DMA 在每个 TIM3_CH3 比较事件将下一项写入 CCR3，CPU 不参与 bit 级时序。

DMA 以 Normal 模式发送一帧后触发完成回调。回调停止 PWM DMA，并令 CCR3 为 0，使数据线保持低电平。下一次刷新最早由 20 ms 周期的 LED 任务发起，远大于 WS2812 所需的复位低电平时间。

驱动 API：

- `WS2812_Init()`：初始化软件状态，确保输出为低。
- `WS2812_SelectPreset()`：只更新待显示预设，不直接在 UI 任务中启动 DMA。
- `WS2812_GetPreset()`：供 UI 显示当前预设。
- `WS2812_Tick()`：仅由 LED 任务调用；生成静态或动画帧，并在 DMA 空闲时发送。

UI 与 LED 任务共享的预设值为单字节状态；实际 DMA 与像素缓冲仅由 LED 任务拥有，从而避免 UI、DMA 回调和动画同时改写同一缓冲。

## UI 与灯效

主菜单顺序调整为 Attitude、Storage、LED、UART、Setting，令低频的设置/清空操作位于最后。

将现有 LED 菜单中的 `WS2812` 项跳转到新的 `UI_WS2812_PAGE`。该页有两个可编辑字段：模式与颜色。四行 OLED 页面显示标题、当前编辑字段、模式和颜色；编辑字段以反白提示。

- `KEY_UP_SHORT`：在“模式”和“颜色”字段之间切换编辑焦点；
- `KEY0_SHORT`：当前字段的上一个值；
- `KEY1_SHORT`：当前字段的下一个值；
- 每次切换立即生效；
- 任意长按：沿用现有父页面逻辑返回 LED 菜单。

模式为：关闭、固定色、彩虹流水、单色追逐、单色呼吸。颜色为：红、绿、蓝、黄、青、紫、白。彩虹流水忽略颜色字段；其余模式使用所选颜色。这样既能单独选颜色，也能选择颜色相关的动画。

不新增 FreeRTOS 任务。扩展已有 `LED_Task_Entry()`：从 500 ms 改为每 20 ms 调用一次 `WS2812_Tick()`，并继续执行 `Watchdog_Checkin(WDG_TASK_LED)`。这样动画离开 UI 页面后仍可运行，且任务职责仍然是 LED 状态输出。

## 单扇区 UART 历史

历史区保留一个 4 KB 扇区。每条记录使用固定 80 字节格式：magic、单调递增序号、长度、最多 64 字节内容和保留字节。该扇区约能容纳 51 条物理记录。

- 新记录追加到未写入的物理槽位；
- 读取时按序号定位最新的 15 条，对外继续使用 `Storage_ReadHistoryByOffset(offset, &rec)`；
- 扇区写满时，将最新 15 条暂存到 RAM，擦除该扇区，写回这 15 条后再继续追加；
- 元数据将记录下一写入槽位、有效记录数和最新序号；旧的 15 扇区格式不兼容，升级后首次清空历史；
- `Storage_RequestClearHistory()` 仅擦除一个历史扇区并重置元数据。

Storage UI 不再显示 `Empty`。页面只渲染有效历史记录；无有效记录时页面保持空白，且 KEY0/KEY1 不移动选择；有记录时选择范围只覆盖有效条目，不允许移动到空槽位。新增 `Storage_GetHistoryCount()` 供 UI 限制选择范围。

## 看门狗修复

1. 保留 CubeMX 生成的 `MX_IWDG_Init()` 调用位置，不在生成区手动移动。这样严格满足本工程的 CubeMX 用户代码约束；调度器前初始化的 IWDG 边界留给 InitTask 重构统一解决。
2. `History_ClearAll()` 改为只擦除一个历史扇区，并在擦除后调用 `Watchdog_Checkin(WDG_TASK_STORAGE)`。历史压缩或擦除期间也在每个可能耗时的 Flash 操作后打卡。

## 错误处理与验证

- DMA 正在发送时，`WS2812_Tick()` 跳过本轮更新，下一轮再试；不会阻塞 UI、UART 或 Storage。
- 构建验证：CMake Debug 构建零错误。
- 上板验证：依次验证 8 个静态颜色、三种动画、UI 长按返回、UART 接收与 Flash 历史不受影响。
- 看门狗验证：正常空闲不出现 stale 日志；清空历史时不误报 Storage stale；可选地制造一次任务卡死，验证 IWDG 复位诊断日志。

## 非目标

- 本轮不加入 CAN、不保存 WS2812 预设到 Flash。
- 本轮不实现亮度滑条、逐灯编辑或自定义 RGB 数值输入；它们留给后续版本。
- 本轮不引入 InitTask。InitTask 涉及全部任务的启动门控和 CubeMX 生成的 IWDG 调用边界，将作为后续独立架构重构。
- 本轮不改变 IWDG 在调度器前的启动位置；当前上板未复现异常，且移动该生成调用会违反本轮的 CubeMX 代码边界。
