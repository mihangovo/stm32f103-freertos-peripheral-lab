# 看门狗(IWDG+多任务存活性聚合) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给 myFreeRTOS_test 工程加一个能自动从"任务卡死/调度器瘫痪"中恢复的硬件看门狗（IWDG），聚合监控现有全部6个FreeRTOS任务的存活状态。

**Architecture:** 新建一个独立的`WatchdogTask`，每500ms检查一次6个任务各自维护的"最近打卡tick"，全部新鲜(1500ms内)才喂狗(`HAL_IWDG_Refresh`)，否则任由IWDG约3秒后自然复位。已通过CubeMX生成`hiwdg`(Prescaler=64,Reload=1874)和`WatchdogTaskHandle`任务壳。

**Tech Stack:** STM32F103 + FreeRTOS(CMSIS-RTOS2) + HAL库，STM32Cube VS Code插件工具链(cmake/ninja/arm-none-eabi-gcc，见下方Global Constraints里的具体路径)。这是嵌入式硬件项目，**没有单元测试框架**——每个任务的"测试"步骤是编译验证(捕获语法/类型错误)，真正的功能验证放在最后一个任务里用真实硬件手动完成。

## Global Constraints

- 编译工具链不在系统PATH上，每次编译前必须手动拼接：
  - cmake.exe: `C:\Users\arch\AppData\Local\stm32cube\bundles\cmake\4.3.1+st.1\bin\cmake.exe`
  - arm-none-eabi-gcc等: `C:\Users\arch\AppData\Local\stm32cube\bundles\gnu-tools-for-stm32\14.3.1+st.2\bin`
  - ninja.exe: `C:\Users\arch\AppData\Local\stm32cube\bundles\ninja\1.13.2+st.1\bin`
  - 如果按这些路径找不到文件，去 `C:\Users\arch\AppData\Local\stm32cube\bundles\` 下重新确认实际的版本号目录名（插件升级后版本号会变）。
- **每次CubeMX重新生成代码后，`Core/Src/freertos.c`/`Core/Src/usart.c`/`Core/Src/main.c`里的中文注释可能被重新乱码**（已知的、还未根治的工具bug）。本计划里的任务不涉及重新触发CubeMX生成，所以不应该再引入新的乱码；但如果执行过程中不小心又点了CubeMX的Generate Code，生成后要用下面的命令逐个检查这几个文件的编码：
  ```powershell
  python -c "open('Core/Src/freertos.c','rb').read().decode('utf-8'); print('OK')"
  ```
  解码失败说明又乱码了，需要手动把乱码的中文注释重打一遍（对照本计划里给出的原始正确文本）。
- IWDG外设已经在CubeMX里激活（`Core/Src/iwdg.c`里`hiwdg.Init.Prescaler = IWDG_PRESCALER_64; hiwdg.Init.Reload = 1874;`），本计划不修改这部分配置。
- **重要：在Task 6（完整硬件验证）之前，不要把固件烧录到硬件上。** IWDG已经激活但`Watchdog_Task_Entry`在Task 1完成前还是空转不喂狗，Task 1~5之间的中间状态如果烧录，板子会每隔~3秒自动复位，干扰不了别的调试但会造成困惑，编译验证足够，不需要每个任务都烧录。

---

### Task 1: 看门狗核心模块 (watchdog_task.h/.c) + 接入CubeMX生成的任务壳

**Files:**
- Create: `User/TASK/watchdog_task.h`
- Create: `User/TASK/watchdog_task.c`
- Modify: `Core/Src/freertos.c:27-36`（USER CODE BEGIN Includes区域，加一行include）
- Modify: `Core/Src/freertos.c:397-406`（Watchdog_Task壳函数，接入真正实现）

**Interfaces:**
- Produces: `WatchdogTaskId_t`枚举（`WDG_TASK_KEY/UI/MPU/HEARTBEAT/STORAGE/LED/COUNT`），`void Watchdog_Checkin(WatchdogTaskId_t id)`，`void Watchdog_Task_Entry(void *argument)`。后续Task 2~4里，其它5个任务文件会调用`Watchdog_Checkin()`。

- [ ] **Step 1: 创建 `User/TASK/watchdog_task.h`**

```c
#ifndef __WATCHDOG_TASK_H
#define __WATCHDOG_TASK_H

#include "cmsis_os.h"

typedef enum {
    WDG_TASK_KEY = 0,
    WDG_TASK_UI,
    WDG_TASK_MPU,
    WDG_TASK_HEARTBEAT,
    WDG_TASK_STORAGE,
    WDG_TASK_LED,
    WDG_TASK_COUNT
} WatchdogTaskId_t;

// 各被监控任务在自己主循环里调用，登记"我还活着"
void Watchdog_Checkin(WatchdogTaskId_t id);

// 供 freertos.c 里的 Watchdog_Task 调用的实际执行函数
void Watchdog_Task_Entry(void *argument);

#endif
```

- [ ] **Step 2: 创建 `User/TASK/watchdog_task.c`**

```c
#include "watchdog_task.h"
#include "iwdg.h"
#include "stdio.h"

#define WDG_CHECK_PERIOD_MS     500
#define WDG_STALE_THRESHOLD_MS  1500

static volatile uint32_t g_task_alive_tick[WDG_TASK_COUNT];

static const char *g_task_name[WDG_TASK_COUNT] = {
    "Key", "UI", "MPU", "Heartbeat", "Storage", "Led"
};

void Watchdog_Checkin(WatchdogTaskId_t id)
{
    g_task_alive_tick[id] = osKernelGetTickCount();
}

void Watchdog_Task_Entry(void *argument)
{
    for(;;)
    {
        osDelay(WDG_CHECK_PERIOD_MS);

        uint32_t now = osKernelGetTickCount();
        uint8_t all_alive = 1;

        for(int i = 0; i < WDG_TASK_COUNT; i++)
        {
            if((now - g_task_alive_tick[i]) > WDG_STALE_THRESHOLD_MS)
            {
                printf("[Watchdog] task %s stale, skip feed\r\n", g_task_name[i]);
                all_alive = 0;
            }
        }

        if(all_alive)
        {
            HAL_IWDG_Refresh(&hiwdg);
        }
    }
}
```

- [ ] **Step 3: 把`watchdog_task.h`加入 `Core/Src/freertos.c` 的 USER CODE BEGIN Includes**

当前内容（`freertos.c:27-36`）：

```c
/* USER CODE BEGIN Includes */
#include "key_task.h"
#include "oled.h"
#include "ui_task.h"
#include "mpu_task.h"
#include "usart.h"
#include "stdio.h"
#include "led_task.h"
#include "storage_task.h"
/* USER CODE END Includes */
```

改成：

```c
/* USER CODE BEGIN Includes */
#include "key_task.h"
#include "oled.h"
#include "ui_task.h"
#include "mpu_task.h"
#include "usart.h"
#include "stdio.h"
#include "led_task.h"
#include "storage_task.h"
#include "watchdog_task.h"
/* USER CODE END Includes */
```

- [ ] **Step 4: 接入`Watchdog_Task`壳函数**

当前内容（`freertos.c:397-406`）：

```c
void Watchdog_Task(void *argument)
{
  /* USER CODE BEGIN Watchdog_Task */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END Watchdog_Task */
}
```

改成：

```c
void Watchdog_Task(void *argument)
{
  /* USER CODE BEGIN Watchdog_Task */
  Watchdog_Task_Entry(argument);
  /* USER CODE END Watchdog_Task */
}
```

- [ ] **Step 5: 编译验证**

```powershell
$env:PATH = "C:\Users\arch\AppData\Local\stm32cube\bundles\gnu-tools-for-stm32\14.3.1+st.2\bin;C:\Users\arch\AppData\Local\stm32cube\bundles\ninja\1.13.2+st.1\bin;$env:PATH"
& "C:\Users\arch\AppData\Local\stm32cube\bundles\cmake\4.3.1+st.1\bin\cmake.exe" --build --preset Debug
```

Expected: 编译成功，0 error(s)。此时`WatchdogTask`已经在跑，但因为还没有任何任务调用`Watchdog_Checkin()`，`g_task_alive_tick`全是0——`Watchdog_Task_Entry`会持续判定全部6个任务"stale"、永远不喂狗（这是预期行为，符合设计：没人打卡=当成卡死处理）。**不要烧录**，继续下一个任务。

- [ ] **Step 6: 提交**

```bash
git add User/TASK/watchdog_task.h User/TASK/watchdog_task.c Core/Src/freertos.c
git commit -m "feat: 新增WatchdogTask核心模块(存活时间戳聚合+IWDG喂狗)"
```

---

### Task 2: 4个固定周期任务接入打卡 (Key/MPU/Heartbeat/Led)

**Files:**
- Modify: `User/TASK/key_task.c:1,26-27`
- Modify: `User/TASK/mpu_task.c:1,23-24`
- Modify: `User/TASK/led_task.c:1,47-49`
- Modify: `Core/Src/freertos.c:318-319`（Hearbeat_Task循环）

**Interfaces:**
- Consumes: Task 1 produ的 `Watchdog_Checkin(WatchdogTaskId_t id)`、`WDG_TASK_KEY`/`WDG_TASK_MPU`/`WDG_TASK_HEARTBEAT`/`WDG_TASK_LED`。

- [ ] **Step 1: `User/TASK/key_task.c` 加include和打卡**

文件顶部（`key_task.c:1-2`）当前：

```c
#include "key_task.h"
#include "main.h"
```

改成：

```c
#include "key_task.h"
#include "main.h"
#include "watchdog_task.h"
```

`Key_Scan_Task_Entry`循环开头（`key_task.c:26-28`）当前：

```c
    for(;;)
    {
        for(int i = 0; i < 3; i++)
```

改成：

```c
    for(;;)
    {
        Watchdog_Checkin(WDG_TASK_KEY);

        for(int i = 0; i < 3; i++)
```

- [ ] **Step 2: `User/TASK/mpu_task.c` 加include和打卡**

文件顶部（`mpu_task.c:1-7`）当前：

```c
#include "mpu_task.h"
#include "main.h"
#include "mpu6050.h"
#include "inv_mpu.h"
#include "inv_mpu_dmp_motion_driver.h"
#include "stdio.h"
#include "task.h"
```

改成（新增一行include）：

```c
#include "mpu_task.h"
#include "main.h"
#include "mpu6050.h"
#include "inv_mpu.h"
#include "inv_mpu_dmp_motion_driver.h"
#include "stdio.h"
#include "task.h"
#include "watchdog_task.h"
```

`MPU_Read_Task_Entry`循环开头（`mpu_task.c:23-25`）当前：

```c
    for(;;)
    {
        osMutexAcquire(I2CMutexHandle, osWaitForever);
```

改成：

```c
    for(;;)
    {
        Watchdog_Checkin(WDG_TASK_MPU);

        osMutexAcquire(I2CMutexHandle, osWaitForever);
```

- [ ] **Step 3: `User/TASK/led_task.c` 加include和打卡**

文件顶部（`led_task.c:1-4`）当前：

```c
#include "led_task.h"
#include "main.h"
#include "storage_task.h"
#include "stdio.h"
```

改成：

```c
#include "led_task.h"
#include "main.h"
#include "storage_task.h"
#include "stdio.h"
#include "watchdog_task.h"
```

`LED_Task_Entry`循环（`led_task.c:47-50`）当前：

```c
    for(;;)
    {
        osDelay(500);   // 这个任务目前不需要持续工作，红灯只在按键触发时切换状态，主循环留空即可
    }
```

改成：

```c
    for(;;)
    {
        Watchdog_Checkin(WDG_TASK_LED);
        osDelay(500);   // 这个任务目前不需要持续工作，红灯只在按键触发时切换状态，主循环留空即可
    }
```

- [ ] **Step 4: `Core/Src/freertos.c` 里 `Hearbeat_Task` 加打卡**

当前（`freertos.c:312-319`）：

```c
void Hearbeat_Task(void *argument)
{
  /* USER CODE BEGIN Hearbeat_Task */
  /* Infinite loop */
  uint8_t i = 0;
  uint8_t hwm_counter = 0;
  for(;;)
  {
    // oled_Refresh();
```

改成：

```c
void Hearbeat_Task(void *argument)
{
  /* USER CODE BEGIN Hearbeat_Task */
  /* Infinite loop */
  uint8_t i = 0;
  uint8_t hwm_counter = 0;
  for(;;)
  {
    Watchdog_Checkin(WDG_TASK_HEARTBEAT);
    // oled_Refresh();
```

- [ ] **Step 5: 编译验证**

```powershell
$env:PATH = "C:\Users\arch\AppData\Local\stm32cube\bundles\gnu-tools-for-stm32\14.3.1+st.2\bin;C:\Users\arch\AppData\Local\stm32cube\bundles\ninja\1.13.2+st.1\bin;$env:PATH"
& "C:\Users\arch\AppData\Local\stm32cube\bundles\cmake\4.3.1+st.1\bin\cmake.exe" --build --preset Debug
```

Expected: 编译成功，0 error(s)。

- [ ] **Step 6: 提交**

```bash
git add User/TASK/key_task.c User/TASK/mpu_task.c User/TASK/led_task.c Core/Src/freertos.c
git commit -m "feat: Key/MPU/Heartbeat/Led任务接入看门狗打卡"
```

---

### Task 3: Storage_Task 改有限等待 + 接入打卡

**Files:**
- Modify: `User/TASK/storage_task.c:1-7,209-228`

**Interfaces:**
- Consumes: `Watchdog_Checkin(WatchdogTaskId_t id)`、`WDG_TASK_STORAGE`（来自Task 1的`watchdog_task.h`）。

- [ ] **Step 1: 加include**

文件顶部（`storage_task.c:1-7`）当前：

```c
#include "storage_task.h"
#include "norflash.h"
#include <string.h>
#include "stdio.h"
extern osMutexId_t SPIMutexHandle;       // 需要在CubeMX里新建
extern osMutexId_t MetaDataMutexHandle;  // 需要在CubeMX里新建
extern osMessageQueueId_t StorageCmdQueueHandle;  // 需要在CubeMX里新建
```

改成：

```c
#include "storage_task.h"
#include "norflash.h"
#include <string.h>
#include "stdio.h"
#include "watchdog_task.h"
extern osMutexId_t SPIMutexHandle;       // 需要在CubeMX里新建
extern osMutexId_t MetaDataMutexHandle;  // 需要在CubeMX里新建
extern osMessageQueueId_t StorageCmdQueueHandle;  // 需要在CubeMX里新建
```

- [ ] **Step 2: `Storage_Task_Entry` 把 `osWaitForever` 改成1000ms超时，超时和收到命令都打卡**

当前（`storage_task.c:209-228`）：

```c
    for(;;)
    {
        if(osMessageQueueGet(StorageCmdQueueHandle, &cmd, NULL, osWaitForever) == osOK)
        {
            printf("[StorageTask] dequeued cmd.type=%d\r\n", (int)cmd.type);
            switch(cmd.type)
            {
                case STORAGE_CMD_SAVE_STATE:
                printf("save state\r\n");
                    Meta_Save();
                    break;
                case STORAGE_CMD_SAVE_HISTORY:
                    History_Append(cmd.history_str, cmd.history_len);
                    break;
                case STORAGE_CMD_CLEAR_HISTORY:
                    History_ClearAll();
                    break;
            }
        }
    }
```

改成：

```c
    for(;;)
    {
        osStatus_t status = osMessageQueueGet(StorageCmdQueueHandle, &cmd, NULL, 1000);
        Watchdog_Checkin(WDG_TASK_STORAGE);

        if(status == osOK)
        {
            printf("[StorageTask] dequeued cmd.type=%d\r\n", (int)cmd.type);
            switch(cmd.type)
            {
                case STORAGE_CMD_SAVE_STATE:
                printf("save state\r\n");
                    Meta_Save();
                    break;
                case STORAGE_CMD_SAVE_HISTORY:
                    History_Append(cmd.history_str, cmd.history_len);
                    break;
                case STORAGE_CMD_CLEAR_HISTORY:
                    History_ClearAll();
                    break;
            }
        }
    }
```

- [ ] **Step 3: 编译验证**

```powershell
$env:PATH = "C:\Users\arch\AppData\Local\stm32cube\bundles\gnu-tools-for-stm32\14.3.1+st.2\bin;C:\Users\arch\AppData\Local\stm32cube\bundles\ninja\1.13.2+st.1\bin;$env:PATH"
& "C:\Users\arch\AppData\Local\stm32cube\bundles\cmake\4.3.1+st.1\bin\cmake.exe" --build --preset Debug
```

Expected: 编译成功，0 error(s)。

- [ ] **Step 4: 提交**

```bash
git add User/TASK/storage_task.c
git commit -m "feat: StorageTask改有限等待+接入看门狗打卡"
```

---

### Task 4: UI_Manager_Task 改有限等待 + 接入打卡

**Files:**
- Modify: `User/TASK/ui_task.c:1-11,509-514`

**Interfaces:**
- Consumes: `Watchdog_Checkin(WatchdogTaskId_t id)`、`WDG_TASK_UI`（来自Task 1的`watchdog_task.h`）。

- [ ] **Step 1: 加include**

文件顶部（`ui_task.c:1-11`）当前：

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
#include "usart.h"
#include <string.h>
```

改成：

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
#include "usart.h"
#include "watchdog_task.h"
#include <string.h>
```

- [ ] **Step 2: `UI_Manager_Task_Entry` 主循环把 `osWaitForever` 改成1000ms超时并打卡**

当前（`ui_task.c:509-514`）：

```c
    for(;;)
    {
        uint32_t timeout = (ui_pages[current_ui].on_tick != NULL) ? 1 : osWaitForever;
        osStatus_t status = osMessageQueueGet(KeyQueueHandle, &evt, NULL, timeout);

        if(status == osOK)
```

改成：

```c
    for(;;)
    {
        uint32_t timeout = (ui_pages[current_ui].on_tick != NULL) ? 1 : 1000;
        osStatus_t status = osMessageQueueGet(KeyQueueHandle, &evt, NULL, timeout);
        Watchdog_Checkin(WDG_TASK_UI);

        if(status == osOK)
```

- [ ] **Step 3: 编译验证**

```powershell
$env:PATH = "C:\Users\arch\AppData\Local\stm32cube\bundles\gnu-tools-for-stm32\14.3.1+st.2\bin;C:\Users\arch\AppData\Local\stm32cube\bundles\ninja\1.13.2+st.1\bin;$env:PATH"
& "C:\Users\arch\AppData\Local\stm32cube\bundles\cmake\4.3.1+st.1\bin\cmake.exe" --build --preset Debug
```

Expected: 编译成功，0 error(s)。此时全部6个任务都已经接入打卡，理论上`WatchdogTask`应该已经能正常喂狗了，但仍先不烧录——先完成Task 5的复位诊断，一起在Task 6里烧录验证。

- [ ] **Step 4: 提交**

```bash
git add User/TASK/ui_task.c
git commit -m "feat: UIManagerTask改有限等待+接入看门狗打卡"
```

---

### Task 5: 开机复位原因诊断

**Files:**
- Modify: `Core/Src/main.c:111-114`

**Interfaces:**
- 无新增函数接口，纯诊断打印。

- [ ] **Step 1: 在 `USER CODE BEGIN 2` 开头加复位原因检测**

当前（`main.c:110-114`）：

```c
  MX_IWDG_Init();
  /* USER CODE BEGIN 2 */

  UART_Rx_Start();
 HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
```

改成：

```c
  MX_IWDG_Init();
  /* USER CODE BEGIN 2 */

  if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST))
  {
      printf("[Watchdog] last reset was caused by IWDG (task hang recovered)\r\n");
  }
  __HAL_RCC_CLEAR_RESET_FLAGS();

  UART_Rx_Start();
 HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
```

- [ ] **Step 2: 编译验证**

```powershell
$env:PATH = "C:\Users\arch\AppData\Local\stm32cube\bundles\gnu-tools-for-stm32\14.3.1+st.2\bin;C:\Users\arch\AppData\Local\stm32cube\bundles\ninja\1.13.2+st.1\bin;$env:PATH"
& "C:\Users\arch\AppData\Local\stm32cube\bundles\cmake\4.3.1+st.1\bin\cmake.exe" --build --preset Debug
```

Expected: 编译成功，0 error(s)。

- [ ] **Step 3: 提交**

```bash
git add Core/Src/main.c
git commit -m "feat: 开机检测并打印IWDG复位原因诊断"
```

---

### Task 6: 硬件验证（烧录测试）

**Files:** 无代码改动，纯硬件验证。

- [ ] **Step 1: 烧录固件**

用VS Code的STM32Cube插件烧录（或已有的烧录流程），烧录本计划Task 1~5的完整改动。

- [ ] **Step 2: 验证正常运行无误报**

烧录后打开串口监视，观察至少2分钟：

Expected: 串口日志里**不应该**出现任何`[Watchdog] task ... stale`。特别关注长时间不操作按键、不产生UART历史记录的场景（这时Storage/UI应该靠1000ms超时打卡而不是真实工作打卡，验证没有被误判）。

- [ ] **Step 3: 人为制造一次任务卡死，验证自动恢复**

临时在`User/TASK/led_task.c`的`LED_Task_Entry`循环里，`Watchdog_Checkin(WDG_TASK_LED);`那一行**之前**加一个永不返回的死循环模拟卡死（比如`if(g_led_red_state == 0) { while(1); }`，因为`g_led_red_state`开机默认是0，这样一上电Led任务就会卡死且再也不打卡）。重新编译烧录。

Expected: 大约3.5~5秒后设备自动复位（可以观察到GREEN LED心跳灯短暂消失后重新开始闪烁，或者串口重新打印开机日志），且这次开机日志里能看到：

```
[Watchdog] last reset was caused by IWDG (task hang recovered)
```

- [ ] **Step 4: 清理测试用的死循环代码**

把Step 3里临时加的`while(1)`删掉，恢复`led_task.c`原样，重新编译烧录，确认恢复正常心跳、且这次开机日志**不**出现`[Watchdog] last reset was caused by IWDG`（证明这是一次正常上电，不是看门狗触发的）。

```bash
git diff User/TASK/led_task.c
```

Expected: 无差异（确认临时测试代码已经清理干净，git status显示led_task.c无改动）。

- [ ] **Step 5: 最终确认**

```bash
git log --oneline -6
git status
```

Expected: 能看到Task 1~5的6个commit（含Task 5的诊断提交），工作区干净无残留改动。看门狗功能完整交付。
