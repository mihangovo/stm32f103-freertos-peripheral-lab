# 串口数据 OLED 展示 + UI 页面调度重构 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 `ui_task.c` 的页面调度重构为"页面接口+调度表"模式，并在此基础上新增 Storage 历史记录翻页（含自动换行）和 UART 实时滚动监视页面。

**Architecture:** 每个 UI 页面实现 `on_enter`/`on_key`/`on_tick` 三个静态函数，通过一张按 `UI_State_t` 索引的只读表 `ui_pages[]` 分发；`UI_Manager_Task_Entry` 只负责长按返回、查表分发、页面切换判定。UART 数据通过新增的 `UartLineQueueHandle` 消息队列从 ISR 传给 UI 任务轮询消费，绘制仍然只在 UIManagerTask 内、持有 `I2CMutex` 时进行。

**Tech Stack:** STM32F103ZET6 / FreeRTOS (CMSIS-RTOS v2 API) / STM32 HAL / 无第三方 UI 框架，纯 C。

## Global Constraints

- **CubeMX 生成文件的编辑边界**：`Core/Src/usart.c`、`Core/Inc/usart.h` 是 CubeMX 生成文件，所有改动必须写在已存在的 `/* USER CODE BEGIN xxx */ ... /* USER CODE END xxx */` 区域内，绝不能在区域外添加代码，否则 CubeMX 重新生成时会被覆盖。
- **不使用 CubeMX 的 RTOS 对象生成流程**：新增的 `UartLineQueueHandle` 直接用 `osMessageQueueNew()` 在 `usart.c` 的 `USER CODE` 区域里手动创建，不在 `freertos.c` / `.ioc` 里配置。
- **本项目没有单元测试框架**（无 Unity/CMock/host 侧测试），验证方式是：(a) 每个任务改动后本地编译通过（agent 可自行执行并判断），(b) 需要烧录到实际硬件观察 OLED/串口行为的步骤，必须明确写出"请你烧录后做 XX 操作，确认看到 YY 现象"，由用户在硬件上验证并反馈结果——执行计划的 agent 没有物理硬件访问能力，不能自称"已验证通过"，必须等待用户确认硬件行为后才能把该任务标记为完成。
- **编译命令**：`cmake --build --preset Debug`（在仓库根目录执行），预期输出以 `Build files ...` / 无 `Error` 字样结束；工程开启了 `-Wall -Wextra -Wpedantic`，新增代码不应引入新警告。
- **字体几何**：本工程 OLED 页面统一使用 12 号字体（`OLED_ShowString(..., 12, ...)`），单字符宽度 6px，128px 屏宽下每行最多 21 个字符（`128/6=21`，向下取整）；行高固定按 16px 处理（与现有 `Draw_Generic_Menu` 里 `y = row*16` 的约定一致）。

---

## Task 1: 重构 `ui_task.c` 为页面接口 + 调度表（行为零变化）

**Files:**
- Modify: `User/TASK/ui_task.c`（整体重写文件内容，逻辑保持与现状完全一致，只改变组织方式）
- 不改动：`User/TASK/ui_task.h`、`freertos.c`、其他任何文件

**Interfaces:**
- Produces: 内部类型 `UI_Page_t { void (*on_enter)(void); UI_State_t (*on_key)(uint16_t); void (*on_tick)(void); }`；静态表 `ui_pages[UI_PAGE_COUNT]`。后续 Task 2、Task 3 都在这张表和这些静态函数的基础上继续修改同一个文件。
- Consumes：`ui_task.h` 里已有的 `UI_State_t` 枚举（本任务不新增枚举值）、`key_task.h` 的 `KeyEvent_t`、`storage_task.h`/`led_task.h`/`mpu_task.h` 现有接口（`Storage_ReadHistoryByOffset`、`LED_Red_Toggle`、`LED_Red_GetState`、`g_pitch/g_roll/g_yaw`、`g_mpu_read_period`）。

- [ ] **Step 1: 用下面的完整内容替换 `User/TASK/ui_task.c`**

```c
#include "ui_task.h"
#include "key_task.h"
#include "oled.h"
#include "main.h"
#include "mpu_task.h"
#include "stdio.h"
#include "task.h"
#include "led_task.h"
#include "storage_task.h"

extern osMessageQueueId_t KeyQueueHandle;
extern osMutexId_t I2CMutexHandle;
extern osMutexId_t AttitudeMutexHandle;
extern float g_pitch, g_roll, g_yaw;
extern volatile uint32_t g_mpu_read_period;

#define MENU_VISIBLE_ROWS   4

// 数组下标对应UI_State_t枚举值，值是它的父级页面
static const UI_State_t ui_parent_map[UI_PAGE_COUNT] = {
    [UI_MAIN_MENU]         = UI_MAIN_MENU,
    [UI_ATTITUDE_PAGE]     = UI_MAIN_MENU,
    [UI_STORAGE_PAGE]      = UI_MAIN_MENU,
    [UI_SETTING_PAGE]      = UI_MAIN_MENU,
    [UI_LED_MENU]          = UI_MAIN_MENU,
    [UI_LED_ONBOARD_PAGE]  = UI_LED_MENU,
};

typedef struct {
    const char *name;
    UI_State_t  target;
} MenuEntry_t;

static const MenuEntry_t main_menu[] = {
    {"Attitude", UI_ATTITUDE_PAGE},
    {"Storage",  UI_STORAGE_PAGE},
    {"Setting",  UI_SETTING_PAGE},
    {"LED",      UI_LED_MENU},
};
#define MAIN_MENU_COUNT   (sizeof(main_menu)/sizeof(main_menu[0]))

static const MenuEntry_t led_menu[] = {
    {"Onboard LED", UI_LED_ONBOARD_PAGE},
    {"WS2812",      UI_LED_MENU},
};
#define LED_MENU_COUNT   (sizeof(led_menu)/sizeof(led_menu[0]))

// ---------- 页面接口 ----------
typedef struct {
    void (*on_enter)(void);               // 进入该页面时调用一次
    UI_State_t (*on_key)(uint16_t evt);    // 处理短按事件，返回下一页面；不跳转返回 UI_PAGE_COUNT
    void (*on_tick)(void);                // 可选：短超时轮询时调用，不需要填 NULL
} UI_Page_t;

// ---------- 通用菜单绘制/处理 ----------
static void Draw_Generic_Menu(const MenuEntry_t *entries, uint8_t count, uint8_t cursor, uint8_t scroll_offset)
{
    OLED_Clear();

    uint8_t visible_count = (count - scroll_offset < MENU_VISIBLE_ROWS)
                             ? (count - scroll_offset)
                             : MENU_VISIBLE_ROWS;

    for(uint8_t row = 0; row < visible_count; row++)
    {
        uint8_t item_index = scroll_offset + row;
        uint8_t y = row * 16;

        if(item_index == cursor)
        {
            OLED_FillRect(0, y, 127, 16, 1);
            OLED_ShowString(0, y, (uint8_t*)entries[item_index].name, 12, 0);
        }
        else
        {
            OLED_ShowString(0, y, (uint8_t*)entries[item_index].name, 12, 1);
        }
    }

    OLED_Refresh();
}

static UI_State_t Handle_Generic_Menu(const MenuEntry_t *entries, uint8_t count, uint8_t *cursor, uint8_t *scroll_offset, uint16_t evt)
{
    if(evt == KEY0_SHORT)
    {
        *cursor = (*cursor == 0) ? (count - 1) : (*cursor - 1);
    }
    else if(evt == KEY1_SHORT)
    {
        *cursor = (*cursor + 1) % count;
    }
    else if(evt == KEY_UP_SHORT)
    {
        return entries[*cursor].target;
    }

    if(*cursor < *scroll_offset)
    {
        *scroll_offset = *cursor;
    }
    else if(*cursor >= *scroll_offset + MENU_VISIBLE_ROWS)
    {
        *scroll_offset = *cursor - MENU_VISIBLE_ROWS + 1;
    }

    if(*cursor == 0)
    {
        *scroll_offset = 0;
    }
    else if(*cursor == count - 1)
    {
        *scroll_offset = (count > MENU_VISIBLE_ROWS) ? (count - MENU_VISIBLE_ROWS) : 0;
    }

    return UI_PAGE_COUNT;
}

static uint8_t Is_Long_Press(uint16_t evt)
{
    return (evt == KEY0_LONG || evt == KEY1_LONG || evt == KEY_UP_LONG);
}

// ================= MainMenu 页面 =================
static uint8_t main_cursor = 0, main_scroll = 0;

static void MainMenu_Enter(void)
{
    g_mpu_read_period = 200;
    Draw_Generic_Menu(main_menu, MAIN_MENU_COUNT, main_cursor, main_scroll);
}

static UI_State_t MainMenu_OnKey(uint16_t evt)
{
    UI_State_t target = Handle_Generic_Menu(main_menu, MAIN_MENU_COUNT, &main_cursor, &main_scroll, evt);
    if(target == UI_PAGE_COUNT || target == UI_MAIN_MENU)
    {
        Draw_Generic_Menu(main_menu, MAIN_MENU_COUNT, main_cursor, main_scroll);
    }
    return target;
}

// ================= LedMenu 页面 =================
static uint8_t led_cursor = 0, led_scroll = 0;

static void LedMenu_Enter(void)
{
    g_mpu_read_period = 200;
    Draw_Generic_Menu(led_menu, LED_MENU_COUNT, led_cursor, led_scroll);
}

static UI_State_t LedMenu_OnKey(uint16_t evt)
{
    UI_State_t target = Handle_Generic_Menu(led_menu, LED_MENU_COUNT, &led_cursor, &led_scroll, evt);
    if(target == UI_PAGE_COUNT || target == UI_LED_MENU)
    {
        Draw_Generic_Menu(led_menu, LED_MENU_COUNT, led_cursor, led_scroll);
    }
    return target;
}

// ================= Attitude 页面 =================
static void Draw_Attitude_Page(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, (uint8_t*)"Attitude Info", 12, 1);
    OLED_ShowString(0, 20, (uint8_t*)"P:", 12, 1);
    OLED_ShowString(0, 34, (uint8_t*)"R:", 12, 1);
    OLED_ShowString(0, 48, (uint8_t*)"Y:", 12, 1);
    OLED_Refresh();
}

static void Update_Attitude_Values(void)
{
    float pitch, roll, yaw;

    osMutexAcquire(AttitudeMutexHandle, osWaitForever);
    pitch = g_pitch;
    roll  = g_roll;
    yaw   = g_yaw;
    osMutexRelease(AttitudeMutexHandle);

    OLED_ShowFloatNum(20, 20, pitch, 3, 1, 12, 1);
    OLED_ShowFloatNum(20, 34, roll, 3, 1, 12, 1);
    OLED_ShowFloatNum(20, 48, yaw, 3, 1, 12, 1);
    OLED_Refresh();
}

static void Attitude_Enter(void)
{
    g_mpu_read_period = 20;
    Draw_Attitude_Page();
}

static UI_State_t Attitude_OnKey(uint16_t evt)
{
    (void)evt;
    return UI_PAGE_COUNT;
}

static void Attitude_OnTick(void)
{
    Update_Attitude_Values();
}

// ================= Storage 页面 =================
static void Draw_Storage_Page(void)
{
    HistoryRecord_t rec;

    OLED_Clear();
    OLED_ShowString(0, 0, (uint8_t*)"History", 12, 1);

    if(Storage_ReadHistoryByOffset(0, &rec))
    {
        rec.content[rec.length] = '\0';
        OLED_ShowString(0, 20, (uint8_t*)rec.content, 12, 1);
    }
    else
    {
        OLED_ShowString(0, 20, (uint8_t*)"Empty", 12, 1);
    }

    OLED_ShowString(0, 52, (uint8_t*)"Back", 12, 1);
    OLED_Refresh();
}

static void Storage_Enter(void)
{
    g_mpu_read_period = 200;
    Draw_Storage_Page();
}

static UI_State_t Storage_OnKey(uint16_t evt)
{
    (void)evt;
    return UI_PAGE_COUNT;
}

// ================= Setting 页面 =================
static void Draw_Setting_Page(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, (uint8_t*)"Setting Page", 12, 1);
    OLED_ShowString(0, 52, (uint8_t*)"Back:long press", 12, 1);
    OLED_Refresh();
}

static void Setting_Enter(void)
{
    g_mpu_read_period = 200;
    Draw_Setting_Page();
}

static UI_State_t Setting_OnKey(uint16_t evt)
{
    (void)evt;
    return UI_PAGE_COUNT;
}

// ================= LedOnboard 页面 =================
static void Draw_Led_Onboard_Page(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, (uint8_t*)"Onboard LED", 12, 1);
    OLED_ShowString(0, 24, (uint8_t*)"State:", 12, 1);
    OLED_ShowString(60, 24, (uint8_t*)(LED_Red_GetState() ? "ON " : "OFF"), 12, 1);
    OLED_ShowString(0, 44, (uint8_t*)"KeyUp:Toggle", 12, 1);
    OLED_Refresh();
}

static void LedOnboard_Enter(void)
{
    g_mpu_read_period = 200;
    Draw_Led_Onboard_Page();
}

static UI_State_t LedOnboard_OnKey(uint16_t evt)
{
    if(evt == KEY_UP_SHORT)
    {
        LED_Red_Toggle();
        Draw_Led_Onboard_Page();
    }
    return UI_PAGE_COUNT;
}

// ================= 调度表 =================
static const UI_Page_t ui_pages[UI_PAGE_COUNT] = {
    [UI_MAIN_MENU]        = { MainMenu_Enter,   MainMenu_OnKey,   NULL },
    [UI_ATTITUDE_PAGE]    = { Attitude_Enter,   Attitude_OnKey,   Attitude_OnTick },
    [UI_STORAGE_PAGE]     = { Storage_Enter,    Storage_OnKey,    NULL },
    [UI_SETTING_PAGE]     = { Setting_Enter,    Setting_OnKey,    NULL },
    [UI_LED_MENU]         = { LedMenu_Enter,    LedMenu_OnKey,    NULL },
    [UI_LED_ONBOARD_PAGE] = { LedOnboard_Enter, LedOnboard_OnKey, NULL },
};

void UI_Manager_Task_Entry(void *argument)
{
    UI_State_t current_ui = UI_MAIN_MENU;
    UI_State_t last_ui = UI_PAGE_COUNT;
    uint16_t evt;

    osMutexAcquire(I2CMutexHandle, osWaitForever);
    ui_pages[current_ui].on_enter();
    osMutexRelease(I2CMutexHandle);
    last_ui = current_ui;

    for(;;)
    {
        uint32_t timeout = (ui_pages[current_ui].on_tick != NULL) ? 1 : osWaitForever;
        osStatus_t status = osMessageQueueGet(KeyQueueHandle, &evt, NULL, timeout);

        if(status == osOK)
        {
            if(Is_Long_Press(evt))
            {
                current_ui = ui_parent_map[current_ui];
            }
            else
            {
                UI_State_t target = ui_pages[current_ui].on_key(evt);
                if(target != UI_PAGE_COUNT) current_ui = target;
            }
        }

        if(current_ui != last_ui)
        {
            osMutexAcquire(I2CMutexHandle, osWaitForever);
            ui_pages[current_ui].on_enter();
            osMutexRelease(I2CMutexHandle);
            last_ui = current_ui;
        }
        else if(ui_pages[current_ui].on_tick != NULL)
        {
            osMutexAcquire(I2CMutexHandle, osWaitForever);
            ui_pages[current_ui].on_tick();
            osMutexRelease(I2CMutexHandle);
        }
    }
}
```

- [ ] **Step 2: 编译验证**

Run: `cmake --build --preset Debug`
Expected: 编译成功，无新增 warning/error（`build/Debug/myFreeRTOS_test.elf` 生成时间被更新）。

- [ ] **Step 3: 上板回归测试（需要用户在硬件上确认，agent 不能自行判定通过）**

请烧录后依次验证，确认与重构前行为完全一致：
1. 开机看到主菜单（Attitude/Storage/Setting/LED），KEY0/KEY1 移动光标、KEY_UP 确认进入。
2. 进入 Attitude 页面，看到 P/R/Y 数值持续刷新；长按任意键返回主菜单。
3. 进入 Storage 页面，看到 "History" 标题 + 最新一条记录（或 "Empty"）+ "Back"；长按返回。
4. 进入 Setting 页面，看到 "Setting Page" + "Back:long press"；长按返回。
5. 进入 LED 子菜单 → Onboard LED 页面，看到当前 ON/OFF 状态，按 KEY_UP 能切换板载红灯并更新显示；长按两次能回到主菜单。

请回复确认以上 5 项是否都正常，若有异常告知具体现象。

- [ ] **Step 4: 提交**

```bash
git add User/TASK/ui_task.c
git commit -m "refactor: UI页面调度改为接口+调度表模式，行为不变"
```

---

## Task 2: OLED 自动换行 + Storage 历史记录翻页

**Files:**
- Modify: `User/OLED/oled.h`（新增函数原型 + 字符数常量）
- Modify: `User/OLED/oled.c`（新增 `OLED_ShowStringWrapped` 实现）
- Modify: `User/TASK/ui_task.c`（Task 1 产出的文件；改造 `Draw_Storage_Page`、新增 `Storage_OnKey` 翻页逻辑）

**Interfaces:**
- Consumes：Task 1 产出的 `Storage_Enter`/`Storage_OnKey`/`ui_pages[]` 结构；`storage_task.h` 里已有的 `Storage_ReadHistoryByOffset(uint8_t offset, HistoryRecord_t *out)` 和 `FLASH_HISTORY_SLOT_COUNT`（值为15）。
- Produces：`void OLED_ShowStringWrapped(uint8_t x, uint8_t y, uint8_t *chr, uint8_t size1, uint8_t mode, uint8_t max_lines)`——Task 3 不依赖此函数（UART监视页用独立的滚动缓冲逻辑），但项目里以后任何需要自动换行的页面都可以直接调用。

- [ ] **Step 1: 修改 `User/OLED/oled.h`**，在 `#define OLED_DATA 1` 之后添加：

```c
#define OLED_LINE_CHARS_12PT   21   // 12号字体在128px宽屏幕上一行最多可显示的字符数(128/6)
```

在 `void OLED_FillRect(...)` 声明之后、`#endif` 之前添加：

```c
void OLED_ShowStringWrapped(uint8_t x, uint8_t y, uint8_t *chr, uint8_t size1, uint8_t mode, uint8_t max_lines);
```

- [ ] **Step 2: 修改 `User/OLED/oled.c`**

在文件顶部 `#include "stdlib.h"` 之后添加：

```c
#include <string.h>
```

在 `OLED_ShowString` 函数实现之后添加新函数：

```c
//在指定位置显示一个自动换行的字符串，超出max_lines的内容会被截断
//x,y:起点坐标  size1:字体大小  mode:0反色 1正常  max_lines:最多显示的行数(行高固定16px)
void OLED_ShowStringWrapped(uint8_t x, uint8_t y, uint8_t *chr, uint8_t size1, uint8_t mode, uint8_t max_lines)
{
    uint8_t char_w = (size1 == 8) ? 6 : (size1 / 2);
    uint8_t max_chars_per_line = (128 - x) / char_w;
    uint8_t line_buf[OLED_LINE_CHARS_12PT + 1];
    uint16_t total_len = (uint16_t)strlen((char *)chr);
    uint16_t offset = 0;
    uint8_t line = 0;

    if(max_chars_per_line > OLED_LINE_CHARS_12PT)
    {
        max_chars_per_line = OLED_LINE_CHARS_12PT;
    }

    while((line < max_lines) && (offset < total_len))
    {
        uint16_t remain = total_len - offset;
        uint8_t take = (remain > max_chars_per_line) ? max_chars_per_line : (uint8_t)remain;

        memcpy(line_buf, chr + offset, take);
        line_buf[take] = '\0';

        OLED_ShowString(x, y + line * 16, line_buf, size1, mode);

        offset += take;
        line++;
    }
}
```

- [ ] **Step 3: 编译验证**

Run: `cmake --build --preset Debug`
Expected: 编译成功。此时 `OLED_ShowStringWrapped` 还没有调用者，属于正常的死代码告警豁免（`-Wall` 不会对未使用的非 static 导出函数报警）。

- [ ] **Step 4: 修改 `User/TASK/ui_task.c`**，把 `Draw_Storage_Page`/`Storage_Enter`/`Storage_OnKey` 三个函数整体替换为：

```c
static uint8_t storage_history_offset = 0;   // 0=最新，最大 FLASH_HISTORY_SLOT_COUNT-1

static void Draw_Storage_Page(void)
{
    HistoryRecord_t rec;
    char header[24];

    OLED_Clear();

    snprintf(header, sizeof(header), "History %d/%d", storage_history_offset, FLASH_HISTORY_SLOT_COUNT);
    OLED_ShowString(0, 0, (uint8_t*)header, 12, 1);

    if(Storage_ReadHistoryByOffset(storage_history_offset, &rec))
    {
        rec.content[rec.length] = '\0';
        OLED_ShowStringWrapped(0, 16, (uint8_t*)rec.content, 12, 1, 3);
    }
    else
    {
        OLED_ShowString(0, 16, (uint8_t*)"Empty", 12, 1);
    }

    OLED_Refresh();
}

static void Storage_Enter(void)
{
    g_mpu_read_period = 200;
    storage_history_offset = 0;
    Draw_Storage_Page();
}

static UI_State_t Storage_OnKey(uint16_t evt)
{
    if(evt == KEY0_SHORT)
    {
        if(storage_history_offset < FLASH_HISTORY_SLOT_COUNT - 1) storage_history_offset++;
        Draw_Storage_Page();
    }
    else if(evt == KEY1_SHORT)
    {
        if(storage_history_offset > 0) storage_history_offset--;
        Draw_Storage_Page();
    }
    return UI_PAGE_COUNT;
}
```

（`ui_pages[]` 表里 `UI_STORAGE_PAGE` 一行已经指向 `Storage_Enter`/`Storage_OnKey`，函数签名不变，不需要改表。）

- [ ] **Step 5: 编译验证**

Run: `cmake --build --preset Debug`
Expected: 编译成功，无新增 warning。

- [ ] **Step 6: 上板测试（需要用户在硬件上确认）**

请烧录后：
1. 用串口调试助手连接开发板（115200,8,N,1），依次发送以下几行（每行以回车换行结尾）：
   - `hello`（短内容）
   - `this is a long line over twenty one chars`（超过21字符，验证换行）
   - 一条 60+ 字符的内容（验证3行截断，第64字符及之后应看不到）
   - 再发 2~3 条短内容，凑够至少 5 条方便翻页测试
2. 进入 Storage 页面，确认标题显示 `History 0/15`，内容显示最新一条，超过21字符的自动换成2~3行显示。
3. 按 KEY0 若干次，确认标题里的编号递增、显示内容变为更早发送的记录；按 KEY1 确认编号递减、回到更新的记录。
4. 一直按 KEY0 翻到没有数据的槽位，确认显示 "Empty" 且不会卡死或跳回乱码；翻到 `14/15` 后再按 KEY0 应停在 14，不越界。
5. 长按任意键，确认能正常返回主菜单。

请回复确认以上是否都正常。

- [ ] **Step 7: 提交**

```bash
git add User/OLED/oled.c User/OLED/oled.h User/TASK/ui_task.c
git commit -m "feat: OLED自动换行 + Storage历史记录翻页"
```

---

## Task 3: USART 实时行队列 + UART 监视页面

**Files:**
- Modify: `Core/Inc/usart.h`（USER CODE 区域内新增类型/队列声明）
- Modify: `Core/Src/usart.c`（USER CODE 区域内新增队列创建 + ISR 回调里追加入队）
- Modify: `User/TASK/ui_task.h`（新增 `UI_UART_MONITOR_PAGE` 枚举值）
- Modify: `User/TASK/ui_task.c`（Task 1/2 产出的文件；新增 UART 监视页、菜单项、父页面映射、调度表项）

**Interfaces:**
- Produces：`typedef struct { char text[UART_LINE_MAXLEN]; uint16_t len; } UartLine_t;`、`extern osMessageQueueId_t UartLineQueueHandle;`（`usart.h`，供 `ui_task.c` 使用）。
- Consumes：Task 1 的 `UI_Page_t`/`ui_pages[]`/`ui_parent_map`/`main_menu[]` 结构。

- [ ] **Step 1: 修改 `Core/Inc/usart.h`**

在 `/* USER CODE BEGIN Includes */` 和 `/* USER CODE END Includes */` 之间添加：

```c
#include "cmsis_os.h"
```

在 `/* USER CODE BEGIN Private defines */` 和 `/* USER CODE END Private defines */` 之间添加：

```c
#define UART_LINE_MAXLEN   64

typedef struct {
    char     text[UART_LINE_MAXLEN];
    uint16_t len;
} UartLine_t;

extern osMessageQueueId_t UartLineQueueHandle;
```

- [ ] **Step 2: 修改 `Core/Src/usart.c`**

在 `/* USER CODE BEGIN 1 */` 区域内，紧跟在 `uint8_t uart_rx_buffer[UART_RX_BUFFER_SIZE];` 这一行之后添加：

```c
osMessageQueueId_t UartLineQueueHandle;
```

把 `UART_Rx_Start` 函数体替换为：

```c
void UART_Rx_Start(void)
{
    UartLineQueueHandle = osMessageQueueNew(4, sizeof(UartLine_t), NULL);
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, uart_rx_buffer, UART_RX_BUFFER_SIZE);
    __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);   // 禁用DMA半传输中断，避免不必要的触发
}
```

在 `HAL_UARTEx_RxEventCallback` 函数里，找到下面这段现有代码：

```c
        // 检查末尾是不是\r\n结尾（可选，如果你想严格校验格式）
        if(Size >= 2 && uart_rx_buffer[Size-2] == '\r' && uart_rx_buffer[Size-1] == '\n')
        {
            uint16_t content_len = Size - 2;   // 去掉\r\n，只保留实际内容长度
            Storage_RequestSaveHistory((char*)uart_rx_buffer, content_len);
        }
        else
        {
            // 没有以\r\n结尾，你可以选择依然保存（去掉这个判断），或者丢弃这次数据
            Storage_RequestSaveHistory((char*)uart_rx_buffer, Size);
        }
```

在这段代码之后、`// 重新启动接收，准备接收下一条` 注释之前，插入：

```c
        UartLine_t mon_line;
        uint16_t mon_len = (Size > UART_LINE_MAXLEN) ? UART_LINE_MAXLEN : Size;
        memcpy(mon_line.text, uart_rx_buffer, mon_len);
        mon_line.len = mon_len;
        osMessageQueuePut(UartLineQueueHandle, &mon_line, 0, 0);
```

- [ ] **Step 3: 编译验证**

Run: `cmake --build --preset Debug`
Expected: 编译成功。若报 `memcpy` 隐式声明警告，检查 `usart.c` 顶部是否已有 `#include <string.h>`（当前文件目前没有引入过，需要在 `/* USER CODE BEGIN 0 */` 区域里加一行 `#include <string.h>`）。

- [ ] **Step 4: 修改 `User/TASK/ui_task.h`**，把枚举改为：

```c
typedef enum {
    UI_MAIN_MENU = 0,
    UI_ATTITUDE_PAGE,
    UI_STORAGE_PAGE,
    UI_SETTING_PAGE,
    UI_LED_MENU,
    UI_LED_ONBOARD_PAGE,
    UI_UART_MONITOR_PAGE,
    UI_PAGE_COUNT
} UI_State_t;
```

- [ ] **Step 5: 修改 `User/TASK/ui_task.c`**

在 `#include "storage_task.h"` 之后添加：

```c
#include "usart.h"
#include <string.h>
```

（`UartLineQueueHandle` 的 `extern` 声明已经在 Step 1 加进了 `usart.h`，`ui_task.c` 包含 `usart.h` 后即可直接使用，不需要重复声明。）

把 `ui_parent_map[]` 改为：

```c
static const UI_State_t ui_parent_map[UI_PAGE_COUNT] = {
    [UI_MAIN_MENU]          = UI_MAIN_MENU,
    [UI_ATTITUDE_PAGE]      = UI_MAIN_MENU,
    [UI_STORAGE_PAGE]       = UI_MAIN_MENU,
    [UI_SETTING_PAGE]       = UI_MAIN_MENU,
    [UI_LED_MENU]           = UI_MAIN_MENU,
    [UI_LED_ONBOARD_PAGE]   = UI_LED_MENU,
    [UI_UART_MONITOR_PAGE]  = UI_MAIN_MENU,
};
```

把 `main_menu[]` 改为：

```c
static const MenuEntry_t main_menu[] = {
    {"Attitude", UI_ATTITUDE_PAGE},
    {"Storage",  UI_STORAGE_PAGE},
    {"Setting",  UI_SETTING_PAGE},
    {"LED",      UI_LED_MENU},
    {"UART",     UI_UART_MONITOR_PAGE},
};
```

在 `LedOnboard_OnKey` 函数之后、`// ================= 调度表 =================` 之前，新增一整段：

```c
// ================= UartMonitor 页面 =================
#define UART_MON_LINES   4

static char uart_mon_buf[UART_MON_LINES][OLED_LINE_CHARS_12PT + 1];

static void Draw_Uart_Monitor_Page(void)
{
    OLED_Clear();
    for(uint8_t i = 0; i < UART_MON_LINES; i++)
    {
        OLED_ShowString(0, i * 16, (uint8_t*)uart_mon_buf[i], 12, 1);
    }
    OLED_Refresh();
}

static void UartMon_PushWrapped(const char *text, uint16_t len)
{
    uint16_t offset = 0;

    while(offset < len)
    {
        uint16_t remain = len - offset;
        uint8_t take = (remain > OLED_LINE_CHARS_12PT) ? OLED_LINE_CHARS_12PT : (uint8_t)remain;

        for(uint8_t i = 0; i < UART_MON_LINES - 1; i++)
        {
            memcpy(uart_mon_buf[i], uart_mon_buf[i + 1], sizeof(uart_mon_buf[i]));
        }

        memcpy(uart_mon_buf[UART_MON_LINES - 1], text + offset, take);
        uart_mon_buf[UART_MON_LINES - 1][take] = '\0';

        offset += take;
    }
}

static void UartMon_Enter(void)
{
    g_mpu_read_period = 200;
    Draw_Uart_Monitor_Page();
}

static UI_State_t UartMon_OnKey(uint16_t evt)
{
    (void)evt;
    return UI_PAGE_COUNT;
}

static void UartMon_OnTick(void)
{
    UartLine_t line;
    uint8_t updated = 0;

    while(osMessageQueueGet(UartLineQueueHandle, &line, NULL, 0) == osOK)
    {
        UartMon_PushWrapped(line.text, line.len);
        updated = 1;
    }

    if(updated)
    {
        Draw_Uart_Monitor_Page();
    }
}
```

把 `ui_pages[]` 表改为：

```c
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

- [ ] **Step 6: 编译验证**

Run: `cmake --build --preset Debug`
Expected: 编译成功，无新增 warning。

- [ ] **Step 7: 上板测试（需要用户在硬件上确认）**

请烧录后：
1. 进主菜单，确认新增了 "UART" 这一项（在 LED 下面），KEY0/KEY1/KEY_UP 能正常导航到它。
2. 进入 UART 页面后，用串口调试助手连续发送 5~6 行不同内容（包含至少一行超过21字符的），确认屏幕上呈现"终端滚动"效果：新内容出现在最下面，旧内容依次上移，超过4行的最先发送的内容会从屏幕顶部消失；超过21字符的行会被拆成多行分别滚动进来。
3. 长按任意键，确认能返回主菜单。
4. 回归确认：Storage 页面翻历史记录、Attitude 页面姿态刷新、LED 开关这几项 Task1/Task2 已验证过的功能仍然正常（避免新增菜单项影响了原有页面的父子导航关系）。

请回复确认以上是否都正常。

- [ ] **Step 8: 提交**

```bash
git add Core/Inc/usart.h Core/Src/usart.c User/TASK/ui_task.h User/TASK/ui_task.c
git commit -m "feat: 新增UART实时监视页面（终端式滚动显示）"
```
