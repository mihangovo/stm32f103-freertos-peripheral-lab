# 串口数据 OLED 展示 + UI 页面调度重构 设计文档

日期：2026-07-11

## 背景与目标

当前工程通过 USART1（DMA + 空闲线检测）接收串口数据，收到以 `\r\n` 结尾的一行会存入 W25Q128 Flash 的 15 条历史环形缓冲区（`storage_task.c`），OLED 的 Storage 页面（`ui_task.c` 的 `Draw_Storage_Page`）目前只能显示"最新一条"记录，既不能翻页看更早的记录，也没有针对串口数据的实时展示。

同时，`ui_task.c` 里的 `UI_Manager_Task_Entry` 用一个不断增长的 if-else 链处理所有页面的按键与绘制逻辑，随着页面增多，按键处理逻辑耦合、新增页面需要改动分散、代码可读性和可扩展性下降。

本次目标：
1. 让串口收到的数据既能**实时滚动展示**（终端式日志，最近若干行），也能**翻页浏览**已经落盘到 Flash 的历史记录。
2. 顺带修复历史记录内容超过屏幕宽度时会超出屏幕的问题（自动换行）。
3. 重构 UI 页面调度逻辑为"页面接口 + 调度表"模式，解决按键处理耦合、新增页面麻烦这两个已确认的痛点，同时避免过度设计（不做完整的组件化 UI 框架）。

## 约束

- **CubeMX 生成文件的编辑边界**：`Core/Src/`、`Core/Inc/`、`freertos.c`、`usart.c`、`gpio.c` 等由 STM32CubeMX 生成，只有 `/* USER CODE BEGIN xxx */ ... /* USER CODE END xxx */` 包裹的区域在重新生成代码时会被保留，区域外的手改会被 CubeMX 覆盖。因此：
  - `usart.c` 里的改动必须放在已有的 `USER CODE BEGIN 1` / `USER CODE END 1` 区域内（`HAL_UARTEx_RxEventCallback` 整个函数体已经在这个区域里）。
  - 新增的 `UartLineQueueHandle` 不通过 CubeMX/`freertos.c` 创建，而是直接在 `.c` 文件里用 `osMessageQueueNew()` 手动创建，与 CubeMX 的生成流程完全解耦，避免受生成边界限制（`StorageCmdQueueHandle` 目前是在 `freertos.c` 里创建的，但那是 CubeMX 已经生成好的既有代码，本次新增的队列不复用这个路径）。
  - `User/` 目录下所有文件（`ui_task.c`、`oled.c`、`storage_task.c` 等）都是纯用户代码，不受此约束，可以自由重构。

## 架构设计

### 1. UI 页面调度：页面接口 + 调度表

在 `ui_task.c` 中引入统一的页面接口：

```c
typedef struct {
    void (*on_enter)(void);               // 进入该页面时调用一次
    UI_State_t (*on_key)(uint16_t evt);    // 处理短按/翻页类按键，返回下一页面；不跳转返回 UI_PAGE_COUNT
    void (*on_tick)(void);                 // 可选：短超时轮询时调用（姿态刷新、UART监视页刷新用），不需要填 NULL
} UI_Page_t;

static const UI_Page_t ui_pages[UI_PAGE_COUNT] = {
    [UI_MAIN_MENU]         = { MainMenu_Enter,   MainMenu_OnKey,   NULL },
    [UI_ATTITUDE_PAGE]     = { Attitude_Enter,   Attitude_OnKey,   Attitude_OnTick },
    [UI_STORAGE_PAGE]      = { Storage_Enter,    Storage_OnKey,    NULL },
    [UI_SETTING_PAGE]      = { Setting_Enter,    Setting_OnKey,    NULL },
    [UI_LED_MENU]          = { LedMenu_Enter,    LedMenu_OnKey,    NULL },
    [UI_LED_ONBOARD_PAGE]  = { LedOnboard_Enter, LedOnboard_OnKey, NULL },
    [UI_UART_MONITOR_PAGE] = { UartMon_Enter,    UartMon_OnKey,    UartMon_OnTick },
};
```

`UI_Manager_Task_Entry` 改为瘦分发器：
- 长按事件：按 `ui_parent_map` 跳转到父页面（逻辑不变）。
- 短按事件：调用 `ui_pages[current_ui].on_key(evt)`，返回值非 `UI_PAGE_COUNT` 则切换页面。
- 页面切换时：调用新页面的 `on_enter()`（取代原来分散在 switch 里的 `Draw_Xxx_Page()` 调用和 `g_mpu_read_period` 设置——`g_mpu_read_period` 的设置移到各页面自己的 `on_enter` 里）。
- 每轮循环末尾：若当前页面 `on_tick` 非 NULL 则调用（取代原来单独针对 `UI_ATTITUDE_PAGE` 的特判分支）。

原有的 `Handle_Generic_Menu` / `Draw_Generic_Menu` 保留，作为 `MainMenu_OnKey`/`LedMenu_OnKey` 等的内部实现复用。

新增枚举值 `UI_UART_MONITOR_PAGE`（`ui_task.h`），`ui_parent_map` 中配置其父页面为 `UI_MAIN_MENU`，`main_menu[]` 表新增一行 `{"UART", UI_UART_MONITOR_PAGE}`。

### 2. 数据流：UART 实时数据 → UI

沿用代码库中已有的"ISR 直接调用 `osMessageQueuePut`"模式（现有 `HAL_UARTEx_RxEventCallback` 已经这样给 `StorageCmdQueueHandle` 塞数据）：

- 新增 `UartLineQueueHandle`：`osMessageQueueNew(4, sizeof(UartLine_t), NULL)`，其中：
  ```c
  #define UART_LINE_MAXLEN 64
  typedef struct {
      char     text[UART_LINE_MAXLEN];
      uint16_t len;
  } UartLine_t;
  ```
  在 `usart.c` 顶部（`USER CODE BEGIN 0` 区域）定义/创建该队列（模块级静态初始化或在 `UART_Rx_Start()` 里创建一次）。
- `HAL_UARTEx_RxEventCallback`（`USER CODE BEGIN 1` 区域内）在现有 `Storage_RequestSaveHistory(...)` 调用之外，无条件额外调用一次 `osMessageQueuePut(UartLineQueueHandle, &line, 0, 0)`（超时 0，非阻塞；队列满直接丢弃本条，实时监视不保证不丢包，可靠保存走 Storage 那条路径）。
- `UartMon_OnTick()`（UI 任务侧，仅在停留于 `UI_UART_MONITOR_PAGE` 时被调用，参照现有 Attitude 页面 `timeout=1` 的短超时轮询写法）：以 0 超时调用 `osMessageQueueGet(UartLineQueueHandle, &line, NULL, 0)`，取到就追加进本地滚动缓冲区并重绘；取不到则跳过，不阻塞。

ISR 中只做"入队"，不触碰 OLED/I2C（避免在中断上下文里跑软件 I2C 导致阻塞甚至死锁）；实际绘制仍然只由 `UIManagerTask` 持有 `I2CMutex` 后完成，与现有并发模型一致。

### 3. Storage 页面：历史翻页

- 页面内部状态：`static uint8_t history_offset`（0 = 最新，最大 14）。
- `Storage_OnKey`：`KEY0_SHORT` → `offset+1`（更旧，钳制在 14）；`KEY1_SHORT` → `offset-1`（更新，钳制在 0）；越界不循环，直接停在边界。
- `Storage_ReadHistoryByOffset(offset, &rec)` 返回 0（该槽位未写过/无效）时显示 "Empty"，此时翻页仍可继续（因为你可能想往前翻到更早但已写过的记录，或往后翻回最新）。
- 顶部提示行从固定的 "History" 改为 `History <offset>/15`，告知当前翻到第几条。

### 4. UART 实时监视页面

- 内部维护固定大小的滚动缓冲（环形数组，`char lines[4][WRAP_WIDTH]`）。数量取 4，与 `ui_task.c` 里已有的 `MENU_VISIBLE_ROWS`（12号字体、128x64屏幕一屏可显示4行）保持一致——本页面不额外画标题行，把4行都用来显示日志，没有翻页/回看按键的前提下，缓冲区大小超过可见行数没有意义（暂不做手动回看，属于当前不需要的功能）。
- 新数据到达（`UartMon_OnTick` 从队列取出）：若该行超过屏幕宽度，先用换行工具函数拆成多个"显示行"，再逐行追加进滚动缓冲（追加时最旧的显示行被挤出）。
- 绘制：4 行缓冲区整体显示满屏，新内容追加在最下面、最旧内容从顶部挤出，效果类似终端滚动日志。
- 无按键交互需求（`UartMon_OnKey` 对 `KEY0_SHORT`/`KEY1_SHORT` 不做任何事，返回 `UI_PAGE_COUNT`；长按走通用的返回上级逻辑）。
- 该页面的滚动缓冲只在内存中，不写入 Flash；只要没有重启就会持续累积（即使当前不在此页面，ISR 仍在入队，但队列只有 4 个缓冲位，久不消费会丢弃——这是预期行为，可靠保存请看 Storage 页面）。

### 5. 长文本自动换行（顺带修复）

新增 `OLED_ShowStringWrapped(x, y, str, size1, mode, max_lines)`（`User/OLED/oled.c`/`.h`），按每行可容纳字符数（依据 `size1` 字体宽度与屏幕宽度 128px 计算）切割字符串，逐行调用现有的 `OLED_ShowString`，超过 `max_lines` 的内容截断不显示。Storage 页面和 UART 监视页面的换行都复用这个函数。

## 文件改动范围

| 文件 | 改动类型 | 说明 |
|---|---|---|
| `User/TASK/ui_task.c` | 重构 + 新增 | 页面接口/调度表、`UI_UART_MONITOR_PAGE` 逻辑、Storage 翻页逻辑 |
| `User/TASK/ui_task.h` | 新增 | `UI_UART_MONITOR_PAGE` 枚举值 |
| `User/OLED/oled.c` / `.h` | 新增函数 | `OLED_ShowStringWrapped`，不改动已有函数签名/行为 |
| `Core/Src/usart.c` | 仅 USER CODE 区域内新增 | `UartLineQueueHandle` 定义与创建、`HAL_UARTEx_RxEventCallback` 内追加入队调用 |
| `User/TASK/storage_task.c` | 不改动 | `Storage_ReadHistoryByOffset` 已支持按 offset 读取 |
| `CMakeLists.txt` | 不改动 | 本次不新增源文件，逻辑都放在既有文件内 |

不拆分新文件：UART 监视页与 Storage 翻页逻辑作为 `ui_task.c` 内的静态函数（与现有 `Draw_Attitude_Page` 等风格一致）。`ui_task.c` 当前 314 行，预计增加到 450~500 行左右，仍在可维护范围内，避免为了拆分而拆分。

## 边界情况

- UART 队列满：新数据直接丢弃（`osMessageQueuePut` 超时 0，失败即丢），不影响已有的 Flash 历史保存路径（两条队列投递互相独立，一条满不影响另一条）。
- Storage 页面翻页到没有数据的槽位：显示 "Empty"，允许继续翻页（不锁死翻页方向)。
- 长按返回：`ui_parent_map` 逻辑不变，`UI_UART_MONITOR_PAGE` 的父页面固定为 `UI_MAIN_MENU`。
- 页面切换时 `g_mpu_read_period` 的设置逻辑迁移进各页面的 `on_enter()`，避免遗漏（原来这个赋值和绘制调用是写在同一个 switch 分支里的，重构后要确保每个页面的 `on_enter` 都设置了合适的值，行为与重构前一致）。

## 测试计划

嵌入式项目没有单元测试框架，验证方式为编译通过 + 上板观察：
1. 编译 Debug 配置通过，无新增警告（工程开启了 `-Wall -Wextra -Wpedantic`）。
2. 上板后：主菜单能看到新增的 "UART" 菜单项；进入后用串口调试助手发送几行不同长度（含超过一行宽度）的文本，确认 OLED 上出现终端式滚动、自动换行显示正确、且滚动方向符合预期。
3. 进入 Storage 页面，确认能看到之前发送的记录，用 KEY0/KEY1 翻页能看到不同历史记录，翻到没有数据的槽位显示 "Empty"，翻页在两端正确停住。
4. 回归验证：Attitude 页面姿态刷新、LED 页面开关状态、Setting 页面显示、长按返回上一级菜单等原有行为均未受影响（因为这些页面逻辑是"平移"进新接口，不是重写）。
