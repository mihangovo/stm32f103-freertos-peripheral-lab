# WS2812、单扇区历史与 UI 调整 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为现有 FreeRTOS 工程交付 WS2812 灯环、单扇区最近 15 条 UART 历史，以及对应 UI 和看门狗边界修复。

**Architecture:** TIM3_CH3 以 1.25 us PWM bit cell 和 DMA1 Channel2 输出 WS2812 数据；LED 任务每 20 ms 独占 DMA 刷新。Flash 历史使用一个 4 KB 追加式固定槽位 journal，写满时将最近 15 条压缩回写。UI 只通过稳定的 Storage/WS2812 接口访问状态。

**Tech Stack:** STM32F103ZETx、STM32 HAL、CMSIS-RTOS v2、FreeRTOS、C11、CMake/Ninja、STM32CubeMX。

## Global Constraints

- CubeMX 生成的 `Core/` 文件只在已有 `/* USER CODE BEGIN */` 与 `/* USER CODE END */` 之间加入手写内容；不移动或重写生成函数主体。
- TIM3_CH3/PB0、DMA1 Channel2、Memory-to-Peripheral、Normal、Half Word 配置已由 CubeMX 生成；不手改其生成配置。
- UART RX 继续使用 DMA1 Channel5 Circular，不得改变其实现或 DMA 通道。
- 不新增 FreeRTOS 任务；WS2812 仅由现有 LED 任务刷新。
- 本轮不引入 CAN、InitTask、WS2812 预设持久化、逐灯编辑或亮度滑条。

---

### Task 1: 生成代码收口与单扇区历史接口

**Files:**
- Modify: `Core/Src/main.c: USER CODE BEGIN 2`
- Modify: `User/TASK/storage_task.h`
- Modify: `User/TASK/storage_task.c`
- Test: `build/Debug/myFreeRTOS_test.elf`

**Interfaces:**
- Produces `uint8_t Storage_GetHistoryCount(void)` and preserves `Storage_ReadHistoryByOffset(uint8_t, HistoryRecord_t *)`.

- [ ] **Step 1: Remove stale TIM2 user calls and make the generated TIM3 baseline buildable**

  In `Core/Src/main.c` inside `USER CODE BEGIN 2`, delete only:

  ```c
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 19999*12.5/100);
  ```

  Run: `cmake --build --preset Debug`

  Expected: the previous `htim2 undeclared` compilation error is gone; any remaining error is unrelated to TIM2.

- [ ] **Step 2: Define the fixed-size journal record and metadata fields**

  Replace the history constants and record declaration in `User/TASK/storage_task.h` with:

  ```c
  #define FLASH_HISTORY_SECTOR        1U
  #define FLASH_HISTORY_SLOT_COUNT    15U
  #define HISTORY_JOURNAL_CAPACITY    51U
  #define HISTORY_STR_MAXLEN          64U

  typedef struct {
      uint32_t magic;
      uint32_t sequence;
      uint16_t length;
      char content[HISTORY_STR_MAXLEN];
      uint8_t reserved[6];
  } HistoryRecord_t;
  _Static_assert(sizeof(HistoryRecord_t) == 80U, "history record must be 80 bytes");
  ```

  Extend `MetaData_t` with `history_write_slot`, `history_valid_count`, and `history_sequence`; remove `history_next_index`. Declare:

  ```c
  uint8_t Storage_GetHistoryCount(void);
  ```

- [ ] **Step 3: Implement journal append, compaction, clear and lookup**

  In `User/TASK/storage_task.c`, define:

  ```c
  static uint32_t History_Addr(uint8_t slot)
  {
      return FLASH_HISTORY_SECTOR * FLASH_SECTOR_SIZE
           + (uint32_t)slot * sizeof(HistoryRecord_t);
  }
  ```

  Implement append with this exact sequence: lock metadata, reserve the next slot and sequence, unlock metadata; if `history_write_slot == HISTORY_JOURNAL_CAPACITY`, read the newest 15 records into a local `HistoryRecord_t keep[FLASH_HISTORY_SLOT_COUNT]`, erase `FLASH_HISTORY_SECTOR`, call `Watchdog_Checkin(WDG_TASK_STORAGE)`, rewrite `keep`, then reset `history_write_slot` to the kept count. Write the new record, increment valid count up to 15, and save metadata. `Storage_ReadHistoryByOffset` must scan valid physical slots and select the record with the `(offset + 1)`th greatest sequence. `History_ClearAll` erases only `FLASH_HISTORY_SECTOR`, resets these three metadata fields under lock, calls `Watchdog_Checkin(WDG_TASK_STORAGE)`, then saves metadata.

- [ ] **Step 4: Make StorageTask check in at safe intervals**

  Keep `Storage_Task_Entry`'s 1000 ms queue timeout and call `Watchdog_Checkin(WDG_TASK_STORAGE)` after every queue wait. Do not modify `LED_Task_Entry` in this task because the WS2812 interface is created in Task 3.

- [ ] **Step 5: Build and commit**

  Run: `cmake --build --preset Debug`

  Expected: successful ELF link with no `htim2` reference and no new warnings.

  Commit:

  ```bash
  git add Core/Src/main.c User/TASK/storage_task.h User/TASK/storage_task.c
  git commit -m "refactor: 单扇区保存UART历史记录"
  ```

### Task 2: Storage 与主菜单 UI 收口

**Files:**
- Modify: `User/TASK/ui_task.c`
- Modify: `User/TASK/ui_task.h`
- Test: `build/Debug/myFreeRTOS_test.elf`

**Interfaces:**
- Consumes `Storage_GetHistoryCount()` and `Storage_ReadHistoryByOffset()` from Task 1.
- Produces a main menu whose order is Attitude, Storage, LED, UART, Setting.

- [ ] **Step 1: Replace the main menu array with the final order**

  ```c
  static const MenuEntry_t main_menu[] = {
      {"Attitude", UI_ATTITUDE_PAGE},
      {"Storage",  UI_STORAGE_PAGE},
      {"LED",      UI_LED_MENU},
      {"UART",     UI_UART_MONITOR_PAGE},
      {"Setting",  UI_SETTING_PAGE},
  };
  ```

- [ ] **Step 2: Render only valid history records**

  In `Draw_Storage_Page`, obtain `uint8_t count = Storage_GetHistoryCount();`. If `count == 0`, call `OLED_Clear(); OLED_Refresh();` and return. Loop only while `row < HISTORY_VISIBLE_ROWS && storage_scroll + row < count`; remove the `"Empty"` branch entirely. Keep line clipping to `OLED_LINE_CHARS_12PT`.

- [ ] **Step 3: Limit Storage navigation to existing records**

  In `Storage_Enter`, set both cursor and scroll to zero. In `Storage_OnKey`, get `count`; return without drawing if `count == 0`; use `count - 1` as the maximum cursor value instead of `FLASH_HISTORY_SLOT_COUNT - 1`.

- [ ] **Step 4: Build and commit**

  Run: `cmake --build --preset Debug`

  Expected: successful link. On board, an empty history page contains no `Empty` text, and new records can be browsed only within the valid record count.

  Commit:

  ```bash
  git add User/TASK/ui_task.c User/TASK/ui_task.h
  git commit -m "feat: 调整菜单与单扇区历史显示"
  ```

### Task 3: WS2812 DMA driver and LED task integration

**Files:**
- Create: `User/WS2812/ws2812.h`
- Create: `User/WS2812/ws2812.c`
- Modify: `CMakeLists.txt`
- Modify: `User/TASK/led_task.c`
- Test: `build/Debug/myFreeRTOS_test.elf`

**Interfaces:**
- Produces `WS2812_Init`, `WS2812_SelectMode`, `WS2812_SelectColor`, `WS2812_GetMode`, `WS2812_GetColor`, and `WS2812_Tick`.
- Consumes CubeMX `extern TIM_HandleTypeDef htim3` and `HAL_TIM_PWM_Start_DMA(&htim3, TIM_CHANNEL_3, ...)`.

- [ ] **Step 1: Create the public driver interface**

  ```c
  typedef enum { WS2812_MODE_OFF, WS2812_MODE_STATIC, WS2812_MODE_RAINBOW,
                 WS2812_MODE_CHASE, WS2812_MODE_BREATHE, WS2812_MODE_COUNT } WS2812_Mode_t;
  typedef enum { WS2812_COLOR_RED, WS2812_COLOR_GREEN, WS2812_COLOR_BLUE,
                 WS2812_COLOR_YELLOW, WS2812_COLOR_CYAN, WS2812_COLOR_PURPLE,
                 WS2812_COLOR_WHITE, WS2812_COLOR_COUNT } WS2812_Color_t;
  void WS2812_Init(void);
  void WS2812_SelectMode(WS2812_Mode_t mode);
  void WS2812_SelectColor(WS2812_Color_t color);
  WS2812_Mode_t WS2812_GetMode(void);
  WS2812_Color_t WS2812_GetColor(void);
  void WS2812_Tick(void);
  ```

- [ ] **Step 2: Implement encoder and DMA lifecycle**

  Define `WS2812_LED_COUNT 8U`, `WS2812_BITS_PER_LED 24U`, `WS2812_DMA_LEN 192U`, `WS2812_DUTY_0 25U`, and `WS2812_DUTY_1 50U`. Encode each pixel in GRB order, most significant bit first into `static uint16_t g_pwm_buffer[WS2812_DMA_LEN]`; invoke HAL with `(uint32_t *)g_pwm_buffer` because its API has a 32-bit pointer signature while DMA is configured for Half Word transfers. Start one normal DMA transfer only if a `static volatile uint8_t g_dma_busy` flag is zero. In `HAL_TIM_PWM_PulseFinishedCallback`, guard `htim->Instance == TIM3` and `htim->Channel == HAL_TIM_ACTIVE_CHANNEL_3`, stop DMA, set CCR3 to zero, then clear `g_dma_busy`.

- [ ] **Step 3: Implement deterministic 20 ms animations**

  `WS2812_Tick()` owns the eight-pixel buffer. Off writes zero; Static fills all pixels with selected color; Rainbow applies hue `(pixel * 32 + frame) & 0xFF`; Chase lights `frame % 8` and clears the others; Breathe uses a 0..255..0 triangular brightness. Advance `frame` once per tick and submit only when DMA is idle.

- [ ] **Step 4: Integrate source and initialization**

  Add `User/WS2812/ws2812.c` to `target_sources` and `User/WS2812` to `target_include_directories`. Include `ws2812.h` in `led_task.c`; call `WS2812_Init()` before its loop and replace its loop body with:

  ```c
  for(;;)
  {
      Watchdog_Checkin(WDG_TASK_LED);
      WS2812_Tick();
      osDelay(20);
  }
  ```

  Do not change CubeMX-generated TIM3 code.

- [ ] **Step 5: Build and commit**

  Run: `cmake --build --preset Debug`

  Expected: link succeeds and `DMA1_Channel2_IRQHandler` resolves `hdma_tim3_ch3`.

  Commit:

  ```bash
  git add User/WS2812 CMakeLists.txt User/TASK/led_task.c
  git commit -m "feat: 新增TIM3 DMA驱动WS2812灯环"
  ```

### Task 4: WS2812 configuration page and hardware verification

**Files:**
- Modify: `User/TASK/ui_task.h`
- Modify: `User/TASK/ui_task.c`
- Test: `build/Debug/myFreeRTOS_test.elf`

**Interfaces:**
- Consumes all Task 3 WS2812 APIs.
- Produces `UI_WS2812_PAGE` under `UI_LED_MENU`.

- [ ] **Step 1: Add the page identifier and LED menu target**

  Insert `UI_WS2812_PAGE` before `UI_PAGE_COUNT`; change the LED menu item to `{ "WS2812", UI_WS2812_PAGE }`; set its parent map entry to `UI_LED_MENU`.

- [ ] **Step 2: Add the page state and event handling**

  Maintain `static uint8_t ws_edit_field`, `static WS2812_Mode_t ws_mode`, and `static WS2812_Color_t ws_color`. `KEY_UP_SHORT` toggles field 0/1; `KEY0_SHORT` decrements the selected enum modulo its count; `KEY1_SHORT` increments modulo its count. After every event call both `WS2812_SelectMode(ws_mode)` and `WS2812_SelectColor(ws_color)`.

- [ ] **Step 3: Draw the four-line page**

  Draw title `WS2812`, then `Mode: <name>` and `Color: <name>`, applying `OLED_FillRect` and inverse text to the current editable line. Map mode names exactly to `Off`, `Static`, `Rainbow`, `Chase`, `Breathe`; color names exactly to `Red`, `Green`, `Blue`, `Yellow`, `Cyan`, `Purple`, `White`.

- [ ] **Step 4: Build and perform the board checklist**

  Run: `cmake --build --preset Debug`

  Expected: successful link.

  On board: verify PB0-to-DIN plus common ground; test seven static colors, Off, Rainbow, Chase, Breathe, all UI keys and long-press return; send UART lines during animation; clear history and verify there is no `Storage stale` watchdog log.

- [ ] **Step 5: Commit**

  ```bash
  git add User/TASK/ui_task.c User/TASK/ui_task.h
  git commit -m "feat: 新增WS2812颜色与灯效配置页面"
  ```

## Self-review

- Spec coverage: Task 1 implements the single-sector journal and Storage watchdog checkin; Task 2 implements menu and history rendering; Task 3 implements TIM3/DMA WS2812 and LED task ownership; Task 4 implements the configuration UI and hardware validation.
- Placeholder scan: no TBD/TODO markers or unspecified interfaces remain.
- Type consistency: all WS2812 APIs and enum names introduced in Task 3 are the names consumed in Task 4; Storage API introduced in Task 1 is consumed in Task 2.
