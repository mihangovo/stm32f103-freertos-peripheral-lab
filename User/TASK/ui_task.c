#include "ui_task.h"
#include "key_task.h"
#include "oled.h"
#include "main.h"
#include "mpu_task.h"
#include "stdio.h"
#include "task.h"
#include "led_task.h"
#include "storage_task.h"
#include "usart.h"
#include "watchdog_task.h"
#include "ws2812.h"
#include "system_init.h"
#include "can_task.h"
#include <string.h>

extern osMessageQueueId_t KeyQueueHandle;
extern osMutexId_t I2CMutexHandle;
extern osMutexId_t AttitudeMutexHandle;
extern osMutexId_t MetaDataMutexHandle;
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
    [UI_UART_MONITOR_PAGE] = UI_MAIN_MENU,
    [UI_CAN_PAGE]          = UI_MAIN_MENU,
    [UI_WS2812_MENU]            = UI_LED_MENU,
    [UI_WS2812_BRIGHTNESS_PAGE] = UI_WS2812_MENU,
    [UI_WS2812_COLOR_PAGE]      = UI_WS2812_MENU,
    [UI_WS2812_MODE_PAGE]       = UI_WS2812_MENU,
};

typedef struct {
    const char *name;
    UI_State_t  target;
} MenuEntry_t;

static const MenuEntry_t main_menu[] = {
    {"Attitude", UI_ATTITUDE_PAGE},
    {"Storage",  UI_STORAGE_PAGE},
    {"LED",      UI_LED_MENU},
    {"UART",     UI_UART_MONITOR_PAGE},
    {"CAN",      UI_CAN_PAGE},
    {"Setting",  UI_SETTING_PAGE},
};
#define MAIN_MENU_COUNT   (sizeof(main_menu)/sizeof(main_menu[0]))

static const MenuEntry_t led_menu[] = {
    {"Onboard LED", UI_LED_ONBOARD_PAGE},
    {"WS2812",      UI_WS2812_MENU},
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
// 0=最新的一条，数值越大越旧，跟主菜单一样是"光标+滚动窗口"的列表交互
#define HISTORY_VISIBLE_ROWS   4

static uint8_t storage_cursor = 0;
static uint8_t storage_scroll = 0;

static void Draw_Storage_Page(void)
{
    OLED_Clear();

    uint8_t count = Storage_GetHistoryCount();
    if(count == 0)
    {
        OLED_Refresh();
        return;
    }

    for(uint8_t row = 0; row < HISTORY_VISIBLE_ROWS; row++)
    {
        uint8_t offset = storage_scroll + row;
        if(offset >= count)
        {
            break;
        }

        HistoryRecord_t rec;
        char line[OLED_LINE_CHARS_12PT + 1];

        (void)Storage_ReadHistoryByOffset(offset, &rec);
        uint8_t take = (rec.length > OLED_LINE_CHARS_12PT) ? OLED_LINE_CHARS_12PT : rec.length;
        memcpy(line, rec.content, take);
        line[take] = '\0';

        uint8_t y = row * 16;
        if(offset == storage_cursor)
        {
            OLED_FillRect(0, y, 127, 16, 1);
            OLED_ShowString(0, y, (uint8_t*)line, 12, 0);
        }
        else
        {
            OLED_ShowString(0, y, (uint8_t*)line, 12, 1);
        }
    }

    OLED_Refresh();
}

static void Storage_Enter(void)
{
    g_mpu_read_period = 200;
    storage_cursor = 0;
    storage_scroll = 0;
    Draw_Storage_Page();
}

static UI_State_t Storage_OnKey(uint16_t evt)
{
    uint8_t count = Storage_GetHistoryCount();
    if(count == 0)
    {
        return UI_PAGE_COUNT;
    }

    if(evt == KEY0_SHORT)
    {
        if(storage_cursor > 0) storage_cursor--;
    }
    else if(evt == KEY1_SHORT)
    {
        if(storage_cursor < count - 1) storage_cursor++;
    }

    if(storage_cursor < storage_scroll)
    {
        storage_scroll = storage_cursor;
    }
    else if(storage_cursor >= storage_scroll + HISTORY_VISIBLE_ROWS)
    {
        storage_scroll = storage_cursor - HISTORY_VISIBLE_ROWS + 1;
    }

    Draw_Storage_Page();
    return UI_PAGE_COUNT;
}

// ================= Setting 页面 =================
static void Draw_Setting_Page(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, (uint8_t*)"Setting", 12, 1);
    OLED_ShowString(0, 20, (uint8_t*)"Clear UART History", 12, 1);
    OLED_ShowString(0, 40, (uint8_t*)"KeyUp:Confirm", 12, 1);
    OLED_Refresh();
}

static void Setting_Enter(void)
{
    g_mpu_read_period = 200;
    Draw_Setting_Page();
}

static UI_State_t Setting_OnKey(uint16_t evt)
{
    if(evt == KEY_UP_SHORT)
    {
        Storage_RequestClearHistory();

        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t*)"Setting", 12, 1);
        OLED_ShowString(0, 24, (uint8_t*)"History Cleared!", 12, 1);
        OLED_Refresh();
    }
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

// ================= UartMonitor 页面 =================
// 内存里保留最近 UART_MON_BACKLOG 条(跟Storage的历史槽位数一致)，屏幕一次显示
// UART_MON_VISIBLE_ROWS 条，KEY0/KEY1 移动光标+滚动窗口，交互方式和 Storage 页一致
#define UART_MON_BACKLOG        15
#define UART_MON_VISIBLE_ROWS   4

static char uart_mon_buf[UART_MON_BACKLOG][OLED_LINE_CHARS_12PT + 1];
static uint8_t uart_mon_count = 0;    // 已经写入的行数(0~UART_MON_BACKLOG)
static uint8_t uart_mon_cursor = 0;   // 当前高亮选中的行，0=最新
static uint8_t uart_mon_scroll = 0;   // 可见窗口起始行

static void Draw_Uart_Monitor_Page(void)
{
    OLED_Clear();

    for(uint8_t row = 0; row < UART_MON_VISIBLE_ROWS; row++)
    {
        uint8_t idx = uart_mon_scroll + row;
        if(idx >= uart_mon_count)
        {
            break;
        }

        uint8_t y = row * 16;
        if(idx == uart_mon_cursor)
        {
            OLED_FillRect(0, y, 127, 16, 1);
            OLED_ShowString(0, y, (uint8_t*)uart_mon_buf[idx], 12, 0);
        }
        else
        {
            OLED_ShowString(0, y, (uint8_t*)uart_mon_buf[idx], 12, 1);
        }
    }

    OLED_Refresh();
}

// 新内容总是写进第0行(最新)，其余行依次往下顶，超出 UART_MON_BACKLOG 的最旧一条被顶出去；
// 缓冲区没填满时多出来的行本来就是空字符串，顶来顶去也没有副作用
static void UartMon_PushWrapped(const char *text, uint16_t len)
{
    uint16_t offset = 0;

    while(offset < len)
    {
        uint16_t remain = len - offset;
        uint8_t take = (remain > OLED_LINE_CHARS_12PT) ? OLED_LINE_CHARS_12PT : (uint8_t)remain;

        for(uint8_t i = UART_MON_BACKLOG - 1; i > 0; i--)
        {
            memcpy(uart_mon_buf[i], uart_mon_buf[i - 1], sizeof(uart_mon_buf[i]));
        }

        memcpy(uart_mon_buf[0], text + offset, take);
        uart_mon_buf[0][take] = '\0';

        if(uart_mon_count < UART_MON_BACKLOG)
        {
            uart_mon_count++;
        }

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
    if(uart_mon_count == 0)
    {
        return UI_PAGE_COUNT;
    }

    if(evt == KEY0_SHORT)
    {
        if(uart_mon_cursor > 0) uart_mon_cursor--;
    }
    else if(evt == KEY1_SHORT)
    {
        if(uart_mon_cursor < uart_mon_count - 1) uart_mon_cursor++;
    }

    if(uart_mon_cursor < uart_mon_scroll)
    {
        uart_mon_scroll = uart_mon_cursor;
    }
    else if(uart_mon_cursor >= uart_mon_scroll + UART_MON_VISIBLE_ROWS)
    {
        uart_mon_scroll = uart_mon_cursor - UART_MON_VISIBLE_ROWS + 1;
    }

    Draw_Uart_Monitor_Page();
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

// ================= 调度表 =================
// ================= CAN 页面 =================
static void Can_Enter(void)
{
    CanStats_t stats;
    char line[24];
    Can_GetStats(&stats);

    OLED_Clear();
    OLED_ShowString(0, 0, (uint8_t*)"CAN Loopback", 12, 1);
    snprintf(line, sizeof(line), "TX:%lu RX:%lu", (unsigned long)stats.tx_count, (unsigned long)stats.rx_count);
    OLED_ShowString(0, 16, (uint8_t*)line, 12, 1);
    snprintf(line, sizeof(line), "ERR:%lu DROP:%lu", (unsigned long)stats.error_count, (unsigned long)stats.drop_count);
    OLED_ShowString(0, 32, (uint8_t*)line, 12, 1);
    snprintf(line, sizeof(line), "ID:0x%03lX", (unsigned long)stats.last_id);
    OLED_ShowString(0, 48, (uint8_t*)line, 12, 1);
    OLED_Refresh();
}

static UI_State_t Can_OnKey(uint16_t evt)
{
    (void)evt;
    Can_Enter();
    return UI_PAGE_COUNT;
}

static void Can_OnTick(void)
{
    Can_Enter();
}

// ================= WS2812 页面 =================
static uint8_t ws2812_cursor = 0, ws2812_scroll = 0;
static uint8_t ws_mode_field = 0;

static const MenuEntry_t ws2812_menu[] = {
    {"Brightness", UI_WS2812_BRIGHTNESS_PAGE},
    {"Color",      UI_WS2812_COLOR_PAGE},
    {"Mode",       UI_WS2812_MODE_PAGE},
};
#define WS2812_MENU_COUNT (sizeof(ws2812_menu)/sizeof(ws2812_menu[0]))

static const char *const ws_mode_names[] = {"Static", "Breathe", "Chase"};
static const char *const ws_color_names[] = {"Red", "Green", "Blue", "Yellow", "Cyan", "Purple", "White"};

static void Ws2812_SaveSettings(void)
{
    MetaData_t *meta = Storage_GetMeta();
    uint8_t mode_power = WS2812_META_VALID | ((uint8_t)WS2812_GetMode() & WS2812_META_MODE_MASK);

    if(WS2812_GetPower() != 0U)
    {
        mode_power |= WS2812_META_POWER;
    }

    osMutexAcquire(MetaDataMutexHandle, osWaitForever);
    meta->ws2812_brightness = WS2812_GetBrightness();
    meta->ws2812_color = (uint8_t)WS2812_GetColor();
    meta->ws2812_mode_power = mode_power;
    osMutexRelease(MetaDataMutexHandle);
    Storage_RequestSaveState();
}

static void Ws2812Menu_Enter(void)
{
    Draw_Generic_Menu(ws2812_menu, WS2812_MENU_COUNT, ws2812_cursor, ws2812_scroll);
}

static UI_State_t Ws2812Menu_OnKey(uint16_t evt)
{
    UI_State_t target = Handle_Generic_Menu(ws2812_menu, WS2812_MENU_COUNT, &ws2812_cursor, &ws2812_scroll, evt);
    if(target == UI_PAGE_COUNT || target == UI_WS2812_MENU)
    {
        Draw_Generic_Menu(ws2812_menu, WS2812_MENU_COUNT, ws2812_cursor, ws2812_scroll);
    }
    return target;
}

static void Ws2812Brightness_Enter(void)
{
    char value[8];
    snprintf(value, sizeof(value), "%u%%", (unsigned int)WS2812_GetBrightness());
    OLED_Clear();
    OLED_ShowString(0, 0, (uint8_t*)"Brightness", 12, 1);
    OLED_ShowString(0, 22, (uint8_t*)value, 16, 1);
    OLED_ShowString(0, 50, (uint8_t*)"K0:-  K1:+", 12, 1);
    OLED_Refresh();
}

static UI_State_t Ws2812Brightness_OnKey(uint16_t evt)
{
    uint8_t brightness = WS2812_GetBrightness();
    uint8_t changed = 0U;
    if(evt == KEY0_SHORT && brightness >= 10U)
    {
        WS2812_SetBrightness(brightness - 10U);
        changed = 1U;
    }
    else if(evt == KEY1_SHORT && brightness <= 90U)
    {
        WS2812_SetBrightness(brightness + 10U);
        changed = 1U;
    }
    if(changed != 0U) Ws2812_SaveSettings();
    Ws2812Brightness_Enter();
    return UI_PAGE_COUNT;
}

static void Ws2812Color_Enter(void)
{
    WS2812_Color_t color = WS2812_GetColor();
    OLED_Clear();
    OLED_ShowString(0, 0, (uint8_t*)"Color", 12, 1);
    OLED_ShowString(0, 22, (uint8_t*)ws_color_names[color], 16, 1);
    OLED_ShowString(0, 50, (uint8_t*)"K0:<  K1:>", 12, 1);
    OLED_Refresh();
}

static UI_State_t Ws2812Color_OnKey(uint16_t evt)
{
    WS2812_Color_t color = WS2812_GetColor();
    uint8_t changed = 0U;
    if(evt == KEY0_SHORT)
    {
        color = (color == WS2812_COLOR_RED) ? (WS2812_COLOR_COUNT - 1U) : (color - 1U);
        WS2812_SelectColor(color);
        changed = 1U;
    }
    else if(evt == KEY1_SHORT)
    {
        color = (color + 1U) % WS2812_COLOR_COUNT;
        WS2812_SelectColor(color);
        changed = 1U;
    }
    if(changed != 0U) Ws2812_SaveSettings();
    Ws2812Color_Enter();
    return UI_PAGE_COUNT;
}

static void Ws2812Mode_ShowField(uint8_t y, const char *text, uint8_t selected)
{
    if(selected != 0U)
    {
        OLED_FillRect(0, y, 127, 16, 1);
        OLED_ShowString(0, y, (uint8_t*)text, 12, 0);
    }
    else
    {
        OLED_ShowString(0, y, (uint8_t*)text, 12, 1);
    }
}

static void Ws2812Mode_Enter(void)
{
    char power_line[16];
    char effect_line[20];
    WS2812_Mode_t mode = WS2812_GetMode();
    snprintf(power_line, sizeof(power_line), "Power: %s", WS2812_GetPower() ? "On" : "Off");
    snprintf(effect_line, sizeof(effect_line), "Effect: %s", ws_mode_names[mode]);

    OLED_Clear();
    OLED_ShowString(0, 0, (uint8_t*)"Mode", 12, 1);
    Ws2812Mode_ShowField(20, power_line, ws_mode_field == 0U);
    Ws2812Mode_ShowField(38, effect_line, ws_mode_field == 1U);
    OLED_ShowString(0, 54, (uint8_t*)"Up:Select", 8, 1);
    OLED_Refresh();
}

static UI_State_t Ws2812Mode_OnKey(uint16_t evt)
{
    uint8_t changed = 0U;
    if(evt == KEY_UP_SHORT)
    {
        ws_mode_field ^= 1U;
    }
    else if(ws_mode_field == 0U && (evt == KEY0_SHORT || evt == KEY1_SHORT))
    {
        WS2812_SetPower(!WS2812_GetPower());
        changed = 1U;
    }
    else if(ws_mode_field == 1U && evt == KEY0_SHORT)
    {
        WS2812_Mode_t mode = WS2812_GetMode();
        mode = (mode == WS2812_MODE_STATIC) ? (WS2812_MODE_COUNT - 1U) : (mode - 1U);
        WS2812_SelectMode(mode);
        changed = 1U;
    }
    else if(ws_mode_field == 1U && evt == KEY1_SHORT)
    {
        WS2812_SelectMode((WS2812_GetMode() + 1U) % WS2812_MODE_COUNT);
        changed = 1U;
    }
    if(changed != 0U) Ws2812_SaveSettings();
    Ws2812Mode_Enter();
    return UI_PAGE_COUNT;
}

static const UI_Page_t ui_pages[UI_PAGE_COUNT] = {
    [UI_MAIN_MENU]         = { MainMenu_Enter,   MainMenu_OnKey,   NULL },
    [UI_ATTITUDE_PAGE]     = { Attitude_Enter,   Attitude_OnKey,   Attitude_OnTick },
    [UI_STORAGE_PAGE]      = { Storage_Enter,    Storage_OnKey,    NULL },
    [UI_SETTING_PAGE]      = { Setting_Enter,    Setting_OnKey,    NULL },
    [UI_LED_MENU]          = { LedMenu_Enter,    LedMenu_OnKey,    NULL },
    [UI_LED_ONBOARD_PAGE]  = { LedOnboard_Enter, LedOnboard_OnKey, NULL },
    [UI_UART_MONITOR_PAGE] = { UartMon_Enter,    UartMon_OnKey,    UartMon_OnTick },
    [UI_CAN_PAGE]          = { Can_Enter,        Can_OnKey,        Can_OnTick },
    [UI_WS2812_MENU]            = { Ws2812Menu_Enter,       Ws2812Menu_OnKey,       NULL },
    [UI_WS2812_BRIGHTNESS_PAGE] = { Ws2812Brightness_Enter, Ws2812Brightness_OnKey, NULL },
    [UI_WS2812_COLOR_PAGE]      = { Ws2812Color_Enter,      Ws2812Color_OnKey,      NULL },
    [UI_WS2812_MODE_PAGE]       = { Ws2812Mode_Enter,       Ws2812Mode_OnKey,       NULL },
};

// 把当前页面号写入meta并请求保存，跟led_task.c保存红灯状态用的是同一套模式
static void UI_SavePageState(UI_State_t page)
{
    MetaData_t *meta = Storage_GetMeta();
    osMutexAcquire(MetaDataMutexHandle, osWaitForever);
    meta->last_ui_page = (uint8_t)page;
    osMutexRelease(MetaDataMutexHandle);
    Storage_RequestSaveState();
}

void UI_Manager_Task_Entry(void *argument)
{
    UI_State_t current_ui = UI_MAIN_MENU;
    UI_State_t last_ui = UI_PAGE_COUNT;
    uint16_t evt;

    (void)argument;
    System_Init_WaitReady();

    // 上电时恢复上次所在的页面；数据非法(比如flash是首次使用的默认值)时退回主菜单
    MetaData_t *meta = Storage_GetMeta();
    osMutexAcquire(MetaDataMutexHandle, osWaitForever);
    uint8_t saved_page = meta->last_ui_page;
    osMutexRelease(MetaDataMutexHandle);
    if(saved_page < UI_PAGE_COUNT)
    {
        current_ui = (UI_State_t)saved_page;
    }

    osMutexAcquire(I2CMutexHandle, osWaitForever);
    ui_pages[current_ui].on_enter();
    osMutexRelease(I2CMutexHandle);
    last_ui = current_ui;

    for(;;)
    {
        uint32_t timeout = (ui_pages[current_ui].on_tick != NULL) ? 100 : 1000;
        osStatus_t status = osMessageQueueGet(KeyQueueHandle, &evt, NULL, timeout);
        Watchdog_Checkin(WDG_TASK_UI);

        if(status == osOK)
        {
            if(Is_Long_Press(evt))
            {
                current_ui = ui_parent_map[current_ui];
            }
            else
            {
                osMutexAcquire(I2CMutexHandle, osWaitForever);
                UI_State_t target = ui_pages[current_ui].on_key(evt);
                osMutexRelease(I2CMutexHandle);
                if(target != UI_PAGE_COUNT) current_ui = target;
            }
        }

        if(current_ui != last_ui)
        {
            osMutexAcquire(I2CMutexHandle, osWaitForever);
            ui_pages[current_ui].on_enter();
            osMutexRelease(I2CMutexHandle);
            last_ui = current_ui;
            UI_SavePageState(current_ui);
        }
        else if(ui_pages[current_ui].on_tick != NULL)
        {
            osMutexAcquire(I2CMutexHandle, osWaitForever);
            ui_pages[current_ui].on_tick();
            osMutexRelease(I2CMutexHandle);
        }
    }
}
