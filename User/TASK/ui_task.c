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
extern float g_pitch, g_roll, g_yaw;   // MPU_Read_Task 会更新这几个变量
extern volatile uint32_t g_mpu_read_period;   // UI_Manager_Task 会根据当前界面调整读取频率


// ui_task.c
#define MENU_VISIBLE_ROWS   4   // 一屏能显示3行

// 数组下标对应UI_State_t枚举值，值是它的父级页面
static const UI_State_t ui_parent_map[UI_PAGE_COUNT] = {
    [UI_MAIN_MENU]         = UI_MAIN_MENU,   // 主菜单已经是顶层，长按也还是留在主菜单
    [UI_ATTITUDE_PAGE]     = UI_MAIN_MENU,
    [UI_STORAGE_PAGE]      = UI_MAIN_MENU,
    [UI_SETTING_PAGE]      = UI_MAIN_MENU,
    [UI_LED_MENU]          = UI_MAIN_MENU,
    [UI_LED_ONBOARD_PAGE]  = UI_LED_MENU,    // 板载LED页面的上一级是LED菜单，不是主菜单
};


typedef struct {
    const char *name;
    UI_State_t  target;
} MenuEntry_t;


// 一个"菜单项"包含：显示的名字 + 选中它按确认键要跳到哪个页面

// 主菜单表——以后加菜单项，只需要在这里加一行
static const MenuEntry_t main_menu[] = {
    {"Attitude", UI_ATTITUDE_PAGE},
    {"Storage",  UI_STORAGE_PAGE},
    {"Setting",  UI_SETTING_PAGE},
    {"LED",      UI_LED_MENU},
};
#define MAIN_MENU_COUNT   (sizeof(main_menu)/sizeof(main_menu[0]))

// LED子菜单表
static const MenuEntry_t led_menu[] = {
    {"Onboard LED", UI_LED_ONBOARD_PAGE},
    {"WS2812",      UI_LED_MENU},   // WS2812暂时没做，选中它还是留在本菜单，不跳转
};
#define LED_MENU_COUNT   (sizeof(led_menu)/sizeof(led_menu[0]))

// ---------- 各页面的绘制函数 ----------

// static void Draw_Main_Menu(void)
// {
//     OLED_Clear();
//     OLED_ShowString(0, 0, (uint8_t*)"== Main Menu ==", 12, 1);
//     for(int i = 0; i < MENU_ITEM_COUNT; i++)
//     {
//         uint8_t y = 16 + i * 16;   // 行间距从14改成16，跟字体实际高度对齐
//         if(i == menu_cursor)
//         {
//             OLED_FillRect(0, y, 127, 16, 1);   // 高亮条高度从12改成16
//             OLED_ShowString(0, y, (uint8_t*)menu_items[i], 12, 0);
//         }
//         else
//         {
//             OLED_ShowString(0, y, (uint8_t*)menu_items[i], 12, 1);
//         }
//     }
//     OLED_Refresh();
// }

static void Draw_Led_Onboard_Page(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, (uint8_t*)"Onboard LED", 12, 1);
    OLED_ShowString(0, 24, (uint8_t*)"State:", 12, 1);
    OLED_ShowString(60, 24, (uint8_t*)(LED_Red_GetState() ? "ON " : "OFF"), 12, 1);
    OLED_ShowString(0, 44, (uint8_t*)"KeyUp:Toggle", 12, 1);
    OLED_Refresh();
}

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

// static void Draw_Storage_Page(void)
// {
//     OLED_Clear();
//     OLED_ShowString(0, 0, (uint8_t*)"Storage Page", 12, 1);
//     OLED_ShowString(0, 52, (uint8_t*)"Back:long press", 12, 1);
//     OLED_Refresh();   // ← 补上这一行
// }

static void Draw_Storage_Page(void)
{
    HistoryRecord_t rec;

    OLED_Clear();

    OLED_ShowString(0,0,(uint8_t*)"History",12,1);

    if(Storage_ReadHistoryByOffset(0,&rec))
    {
        rec.content[rec.length] = '\0';

        OLED_ShowString(0,20,(uint8_t*)rec.content,12,1);
    }
    else
    {
        OLED_ShowString(0,20,(uint8_t*)"Empty",12,1);
    }

    OLED_ShowString(0,52,(uint8_t*)"Back",12,1);

    OLED_Refresh();
}

static void Draw_Setting_Page(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, (uint8_t*)"Setting Page", 12, 1);
    OLED_ShowString(0, 52, (uint8_t*)"Back:long press", 12, 1);
    OLED_Refresh();   // ← 补上这一行
}

// ---------- 判断是否是"长按"事件（统一当作返回键）----------
static uint8_t Is_Long_Press(uint16_t evt)
{
    return (evt == KEY0_LONG || evt == KEY1_LONG || evt == KEY_UP_LONG);
}

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

// cursor和scroll_offset都通过指针传入，函数内部会同时更新这两个状态
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

    // ---- 根据cursor调整scroll_offset,确保光标始终在可见窗口内 ----
    if(*cursor < *scroll_offset)
    {
        *scroll_offset = *cursor;   // 光标往上移出了窗口顶部，窗口跟着往上滚
    }
    else if(*cursor >= *scroll_offset + MENU_VISIBLE_ROWS)
    {
        *scroll_offset = *cursor - MENU_VISIBLE_ROWS + 1;   // 光标往下移出了窗口底部，窗口跟着往下滚
    }

    // 特殊情况：如果是从"最后一项"通过KEY1循环跳回"第一项"(0)，scroll_offset也要归零
    if(*cursor == 0)
    {
        *scroll_offset = 0;
    }
    // 同理，如果是从"第一项"通过KEY0循环跳到"最后一项"，滚动窗口要跳到能显示最后一项的位置
    else if(*cursor == count - 1)
    {
        *scroll_offset = (count > MENU_VISIBLE_ROWS) ? (count - MENU_VISIBLE_ROWS) : 0;
    }

    return UI_PAGE_COUNT;
}


void UI_Manager_Task_Entry(void *argument)
{
    UI_State_t current_ui = UI_MAIN_MENU;
    UI_State_t last_ui = UI_PAGE_COUNT;
    uint8_t main_cursor = 0, main_scroll = 0;
    uint8_t led_cursor = 0, led_scroll = 0;
    uint16_t evt;

    osMutexAcquire(I2CMutexHandle, osWaitForever);
    Draw_Generic_Menu(main_menu, MAIN_MENU_COUNT, main_cursor, main_scroll);
    osMutexRelease(I2CMutexHandle);
    last_ui = current_ui;

    for(;;)
    {
        uint32_t timeout = (current_ui == UI_ATTITUDE_PAGE) ? 1 : osWaitForever;
        osStatus_t status = osMessageQueueGet(KeyQueueHandle, &evt, NULL, timeout);

        if(status == osOK)
        {
            if(Is_Long_Press(evt))
            {
                // current_ui = UI_MAIN_MENU;
                 current_ui = ui_parent_map[current_ui];   // 长按返回上一级
            }
            else if(current_ui == UI_MAIN_MENU)
            {
                UI_State_t target = Handle_Generic_Menu(main_menu, MAIN_MENU_COUNT, &main_cursor, &main_scroll, evt);
                if(target != UI_PAGE_COUNT) current_ui = target;

                if(current_ui == UI_MAIN_MENU)
                {
                    osMutexAcquire(I2CMutexHandle, osWaitForever);
                    Draw_Generic_Menu(main_menu, MAIN_MENU_COUNT, main_cursor, main_scroll);
                    osMutexRelease(I2CMutexHandle);
                }
            }
            else if(current_ui == UI_LED_MENU)
            {
                UI_State_t target = Handle_Generic_Menu(led_menu, LED_MENU_COUNT, &led_cursor, &led_scroll, evt);
                if(target != UI_PAGE_COUNT) current_ui = target;

                if(current_ui == UI_LED_MENU)
                {
                    osMutexAcquire(I2CMutexHandle, osWaitForever);
                    Draw_Generic_Menu(led_menu, LED_MENU_COUNT, led_cursor, led_scroll);
                    osMutexRelease(I2CMutexHandle);
                }
            }

            else if (current_ui == UI_LED_ONBOARD_PAGE) {
              if (evt == KEY_UP_SHORT) {
                LED_Red_Toggle();

                osMutexAcquire(I2CMutexHandle, osWaitForever);
                Draw_Led_Onboard_Page();
                osMutexRelease(I2CMutexHandle);
              }
            }

            // ... LED_ONBOARD_PAGE分支不变
        }

        if(current_ui != last_ui)
        {
            osMutexAcquire(I2CMutexHandle, osWaitForever);
            switch(current_ui)
            {
                case UI_MAIN_MENU:  g_mpu_read_period = 200; Draw_Generic_Menu(main_menu, MAIN_MENU_COUNT, main_cursor, main_scroll); break;
                case UI_LED_MENU:   g_mpu_read_period = 200; Draw_Generic_Menu(led_menu, LED_MENU_COUNT, led_cursor, led_scroll);       break;
                case UI_ATTITUDE_PAGE:    g_mpu_read_period = 20;  Draw_Attitude_Page();     break;
                case UI_STORAGE_PAGE:     g_mpu_read_period = 200; Draw_Storage_Page();       break;
                case UI_SETTING_PAGE:     g_mpu_read_period = 200; Draw_Setting_Page();       break;
                case UI_LED_ONBOARD_PAGE: g_mpu_read_period = 200; Draw_Led_Onboard_Page();   break;
                default: g_mpu_read_period = 2000; break;
            }
            osMutexRelease(I2CMutexHandle);
            last_ui = current_ui;
        }
        else if(current_ui == UI_ATTITUDE_PAGE)
        {
            osMutexAcquire(I2CMutexHandle, osWaitForever);
            Update_Attitude_Values();
            osMutexRelease(I2CMutexHandle);
        }
    }
}