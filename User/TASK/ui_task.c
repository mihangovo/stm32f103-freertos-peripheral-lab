#include "ui_task.h"
#include "key_task.h"
#include "oled.h"
#include "main.h"
#include "mpu_task.h"
#include "stdio.h"
#include "task.h"

extern osMessageQueueId_t KeyQueueHandle;
extern osMutexId_t I2CMutexHandle;
extern osMutexId_t AttitudeMutexHandle;
extern float g_pitch, g_roll, g_yaw;   // MPU_Read_Task 会更新这几个变量
extern volatile uint32_t g_mpu_read_period;   // UI_Manager_Task 会根据当前界面调整读取频率

static const char *menu_items[] = {
    "Attitude",
    "Storage",
    "Setting",
    "LED",
};
#define MENU_ITEM_COUNT   3

static uint8_t menu_cursor = 0;   // 主菜单当前光标位置

// ---------- 各页面的绘制函数 ----------

static void Draw_Main_Menu(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, (uint8_t*)"== Main Menu ==", 12, 1);
    for(int i = 0; i < MENU_ITEM_COUNT; i++)
    {
        uint8_t y = 16 + i * 16;   // 行间距从14改成16，跟字体实际高度对齐
        if(i == menu_cursor)
        {
            OLED_FillRect(0, y, 127, 16, 1);   // 高亮条高度从12改成16
            OLED_ShowString(0, y, (uint8_t*)menu_items[i], 12, 0);
        }
        else
        {
            OLED_ShowString(0, y, (uint8_t*)menu_items[i], 12, 1);
        }
    }
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

static void Draw_Storage_Page(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, (uint8_t*)"Storage Page", 12, 1);
    OLED_ShowString(0, 52, (uint8_t*)"Back:long press", 12, 1);
    OLED_Refresh();   // ← 补上这一行
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

void UI_Manager_Task_Entry(void *argument)
{
    UI_State_t current_ui = UI_MAIN_MENU;
    UI_State_t last_ui = (UI_State_t)0xFF;   // 强制第一次刷新
    uint16_t evt;

    osMutexAcquire(I2CMutexHandle, osWaitForever);
    Draw_Main_Menu();
    osMutexRelease(I2CMutexHandle);
    last_ui = current_ui;   // 标记已经画过了,避免循环里重复画

    for(;;)
    {
        // 姿态页面需要持续刷新数值，所以不能永久阻塞等待，改用100ms超时
        uint32_t timeout = (current_ui == UI_ATTITUDE_PAGE) ? 1 : osWaitForever;

        osStatus_t status = osMessageQueueGet(KeyQueueHandle, &evt, NULL, timeout);

        if(status == osOK)
        {
            if(Is_Long_Press(evt))
            {
                current_ui = UI_MAIN_MENU;   // 长按统一返回主菜单
            }
            else if(current_ui == UI_MAIN_MENU)
            {
                if(evt == KEY0_SHORT)
                    menu_cursor = (menu_cursor == 0) ? (MENU_ITEM_COUNT - 1) : (menu_cursor - 1);
                else if(evt == KEY1_SHORT)
                    menu_cursor = (menu_cursor + 1) % MENU_ITEM_COUNT;
                else if(evt == KEY_UP_SHORT)
                    current_ui = (UI_State_t)(menu_cursor + 1);   // 进入选中的页面
                if(current_ui == UI_MAIN_MENU)
                    Draw_Main_Menu();   // 菜单里任何操作都要重绘一次（光标移动也要看到变化）
            }
            // 子页面里，短按事件暂时不处理，先留空，以后可以扩展具体功能
        }

        // ---------- 界面切换检测，只在切换时重绘一次静态内容 ----------
        if(current_ui != last_ui)
        {
            osMutexAcquire(I2CMutexHandle, osWaitForever);
            switch(current_ui)
            {
                case UI_MAIN_MENU:       g_mpu_read_period = 200;Draw_Main_Menu();     break;
                case UI_ATTITUDE_PAGE:   g_mpu_read_period = 20;Draw_Attitude_Page();  break;
                case UI_STORAGE_PAGE:    g_mpu_read_period = 200;Draw_Storage_Page();   break;
                case UI_SETTING_PAGE:    g_mpu_read_period = 200;Draw_Setting_Page();   break;
                default: break;
            }
            osMutexRelease(I2CMutexHandle);
            last_ui = current_ui;
        }
        else if(current_ui == UI_ATTITUDE_PAGE)
        {
            // 停留在姿态页面时，持续刷新数值部分
            osMutexAcquire(I2CMutexHandle, osWaitForever);
            Update_Attitude_Values();
            osMutexRelease(I2CMutexHandle);
        }
//         UBaseType_t ui_free = uxTaskGetStackHighWaterMark(NULL);
// printf("UI task stack free: %lu words\r\n", (unsigned long)ui_free);
    }
}