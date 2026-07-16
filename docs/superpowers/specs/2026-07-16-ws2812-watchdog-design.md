# WS2812 灯环与看门狗边界修复设计

## 目标

在现有 STM32F103ZETx + FreeRTOS 工程中加入一个 8 灯 WS2812 兼容 RGB 灯环，并保留已有 UART DMA、Flash、OLED 和看门狗功能。LED 菜单新增 WS2812 配置页，支持固定颜色与动态灯效。

同时修复两个看门狗边界：IWDG 在调度器启动前过早启动，以及清空 15 个 Flash 历史扇区期间 StorageTask 未打卡。

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

将现有 LED 菜单中的 `WS2812` 项跳转到新的 `UI_WS2812_PAGE`。该页使用单一“预设”选择模型，避免在三按键 UI 中引入多层编辑状态：

- `KEY0_SHORT`：上一个预设；
- `KEY1_SHORT`：下一个预设；
- 每次切换立即生效；
- 任意长按：沿用现有父页面逻辑返回 LED 菜单。

首版预设依次为：关闭、红、绿、蓝、黄、青、紫、白、彩虹流水、单色追逐、单色呼吸。静态与单色动画使用当前选中的基础颜色；彩虹流水自行生成颜色相位。

不新增 FreeRTOS 任务。扩展已有 `LED_Task_Entry()`：从 500 ms 改为每 20 ms 调用一次 `WS2812_Tick()`，并继续执行 `Watchdog_Checkin(WDG_TASK_LED)`。这样动画离开 UI 页面后仍可运行，且任务职责仍然是 LED 状态输出。

## 看门狗修复

1. 将 `MX_IWDG_Init()` 从现有外设初始化序列移至 `USER CODE BEGIN 2` 末尾、`osKernelInitialize()` 前。这样 MPU/DMP、OLED、NOR Flash 初始化与 `Meta_Load()` 失败或长时间重试时不会被 IWDG 强制重启；调度器启动后的多任务存活聚合逻辑不变。
2. 在 `History_ClearAll()` 的每次 `norflash_erase_sector()` 后调用 `Watchdog_Checkin(WDG_TASK_STORAGE)`。这使用户触发的连续擦除仍被认定为 StorageTask 正常存活。

## 错误处理与验证

- DMA 正在发送时，`WS2812_Tick()` 跳过本轮更新，下一轮再试；不会阻塞 UI、UART 或 Storage。
- 构建验证：CMake Debug 构建零错误。
- 上板验证：依次验证 8 个静态颜色、三种动画、UI 长按返回、UART 接收与 Flash 历史不受影响。
- 看门狗验证：正常空闲不出现 stale 日志；清空历史时不误报 Storage stale；可选地制造一次任务卡死，验证 IWDG 复位诊断日志。

## 非目标

- 本轮不加入 CAN、不保存 WS2812 预设到 Flash。
- 本轮不实现亮度滑条、逐灯编辑或自定义 RGB 数值输入；它们留给后续版本。
