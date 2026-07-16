# WS2812 UI and Watchdog Boot Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent DMP startup failures from causing IWDG reset loops and add clear WS2812 power, brightness, color, and effect controls.

**Architecture:** Keep CubeMX's `MX_IWDG_Init()` function, but invoke it only after DMP initialization succeeds. Keep WS2812 power, brightness, color, and effect as independent driver state, and replace the combined editor with a three-item WS2812 settings menu.

**Tech Stack:** STM32F103 HAL, CMSIS-RTOS v2, TIM3 PWM + DMA, SSD1306 OLED, PowerShell regression check, CMake Debug build.

## Global Constraints

- Do not alter CubeMX-generated peripheral functions.
- Only the user-authorized `MX_IWDG_Init()` invocation moves in `Core/Src/main.c`.
- Do not stage existing changes in `Core/Src/freertos.c` or `Core/Src/usart.c`.
- WS2812 settings remain runtime-only in this change.

---

### Task 1: Guard watchdog startup order

**Files:**
- Create: `tests/verify_watchdog_startup.ps1`
- Modify: `Core/Src/main.c:109, 173-197`

- [ ] **Step 1: Write a failing source-order regression check**

```powershell
$main = Get-Content "$PSScriptRoot/../Core/Src/main.c" -Raw
$dmp = $main.IndexOf('while (mpu_dmp_init())')
$iwdg = $main.IndexOf('  MX_IWDG_Init();')
$scheduler = $main.IndexOf('  osKernelStart();')
if ($dmp -lt 0 -or $iwdg -lt 0 -or $scheduler -lt 0 -or !($dmp -lt $iwdg -and $iwdg -lt $scheduler)) {
    throw 'MX_IWDG_Init must follow DMP initialization and precede osKernelStart.'
}
```

- [ ] **Step 2: Verify it fails before the fix**

Run: `powershell -ExecutionPolicy Bypass -File tests/verify_watchdog_startup.ps1`

Expected: failure because IWDG currently starts before the DMP retry loop.

- [ ] **Step 3: Implement the minimal startup-order change**

Remove the existing `MX_IWDG_Init();` call from the peripheral initialization list. Add the same call in `USER CODE BEGIN 2` after `Meta_Load();` and before `osKernelInitialize();`.

- [ ] **Step 4: Verify the check and build**

Run: `powershell -ExecutionPolicy Bypass -File tests/verify_watchdog_startup.ps1`

Run: `& 'C:\Users\arch\AppData\Local\stm32cube\bundles\cmake\4.3.1+st.1\bin\cmake.exe' --build --preset Debug`

Expected: the check and Debug build both succeed.

### Task 2: Add independent WS2812 runtime settings

**Files:**
- Modify: `User/WS2812/ws2812.h`
- Modify: `User/WS2812/ws2812.c`

**Interfaces:**
- Produce: `WS2812_SetPower(uint8_t)`, `WS2812_GetPower(void)`, `WS2812_SetBrightness(uint8_t)`, and `WS2812_GetBrightness(void)`.

- [ ] **Step 1: Define the new API**

```c
void WS2812_SetPower(uint8_t on);
uint8_t WS2812_GetPower(void);
void WS2812_SetBrightness(uint8_t brightness);
uint8_t WS2812_GetBrightness(void);
```

- [ ] **Step 2: Build before the UI uses the API**

Run: `& 'C:\Users\arch\AppData\Local\stm32cube\bundles\cmake\4.3.1+st.1\bin\cmake.exe' --build --preset Debug`

Expected: the current code still builds; the API checkpoint introduces no behavior yet.

- [ ] **Step 3: Implement independent state and rendering**

Use `g_power` and `g_brightness` separate from mode and color. Clamp brightness to 100. If power is off, transmit black pixels while retaining all settings. Otherwise scale every generated RGB component by brightness. Remove `WS2812_MODE_OFF`; off is only represented by power.

- [ ] **Step 4: Build the firmware**

Run: `& 'C:\Users\arch\AppData\Local\stm32cube\bundles\cmake\4.3.1+st.1\bin\cmake.exe' --build --preset Debug`

Expected: Debug build succeeds.

### Task 3: Add the three WS2812 settings pages

**Files:**
- Modify: `User/TASK/ui_task.h`
- Modify: `User/TASK/ui_task.c`

**Interfaces:**
- Produce: `UI_WS2812_MENU`, `UI_WS2812_BRIGHTNESS_PAGE`, `UI_WS2812_COLOR_PAGE`, and `UI_WS2812_MODE_PAGE`.

- [ ] **Step 1: Define the settings menu**

```c
static const MenuEntry_t ws2812_menu[] = {
    {"Brightness", UI_WS2812_BRIGHTNESS_PAGE},
    {"Color",      UI_WS2812_COLOR_PAGE},
    {"Mode",       UI_WS2812_MODE_PAGE},
};
```

- [ ] **Step 2: Build to expose undefined page wiring**

Run: `& 'C:\Users\arch\AppData\Local\stm32cube\bundles\cmake\4.3.1+st.1\bin\cmake.exe' --build --preset Debug`

Expected: a compile failure until the parent map and page table contain all new pages.

- [ ] **Step 3: Implement page behavior**

Brightness uses KEY0/KEY1 to decrease/increase by 10 percent. Color uses KEY0/KEY1 to wrap across seven names. Mode uses KEY_UP to select Power or Effect, then KEY0/KEY1 to toggle Power or wrap Static/Rainbow/Chase/Breathe. Each change applies immediately; long press returns to the parent page.

- [ ] **Step 4: Wire navigation and verify build**

Set the three editors' parent to `UI_WS2812_MENU` and the settings menu's parent to `UI_LED_MENU`. Remove the old combined editor and obsolete `UI_WS2812_PAGE` state.

Run: `& 'C:\Users\arch\AppData\Local\stm32cube\bundles\cmake\4.3.1+st.1\bin\cmake.exe' --build --preset Debug`

Expected: Debug build succeeds.

### Task 4: Hardware acceptance and scoped commit

**Files:**
- Modify: `docs/superpowers/plans/2026-07-16-ws2812-ui-watchdog-boot.md`

- [ ] **Step 1: Verify startup behavior on the board**

Force a DMP initialization failure. Confirm the error screen remains visible longer than the previous watchdog timeout without resetting. Restore the MPU connection and confirm boot continues.

- [ ] **Step 2: Verify WS2812 behavior on the board**

Navigate all three pages. Check 0%, 50%, and 100% brightness; every color and effect; then power off and on to verify settings are retained.

- [ ] **Step 3: Inspect and commit only scoped files**

Run: `git diff --check`

Run: `git status --short`

Stage only `Core/Src/main.c`, WS2812/UI files, the regression check, and this plan. Do not stage unrelated encoding changes or personal files. Commit message: `feat: 完善WS2812设置并延后启动看门狗`.
