# WS2812 UI and Watchdog Boot Design

## Goal

Make the WS2812 ring settings easy to use and prevent an MPU/DMP startup
failure from causing repeated independent-watchdog resets.

## Scope

### Watchdog startup

- Keep the current MPU and DMP initialization and retry behavior.
- Do not start the IWDG until `mpu_dmp_init()` has succeeded and all startup
  initialization immediately before the scheduler is complete.
- A failed DMP initialization therefore continues to show the error and retry
  every 200 ms without resetting the board. When the connection recovers, the
  initialization completes and the watchdog is then enabled.
- `MX_IWDG_Init()` remains the CubeMX-generated function. Only its invocation
  in `Core/Src/main.c` moves from the peripheral-initialization sequence to
  the successful-startup path.

### WS2812 driver state

The driver keeps four independent runtime settings:

- power: on or off;
- brightness: 0 to 100 percent in 10 percent increments;
- color: red, green, blue, yellow, cyan, purple, or white;
- effect: static, rainbow, chase, or breathe.

Power-off always transmits a black frame, without changing the other three
settings. Brightness scales every generated RGB component. Rainbow ignores
the selected color but still obeys brightness and power.

### UI hierarchy

`LED -> WS2812` opens a three-item menu:

1. `Brightness`: KEY0/KEY1 decreases/increases brightness by 10 percent.
2. `Color`: KEY0/KEY1 cycles through the seven predefined colors.
3. `Mode`: KEY_UP moves the focus between Power and Effect; KEY0/KEY1 changes
   the focused value.

The existing long-press behavior returns to the parent page. Settings take
effect immediately. This scope deliberately does not add Flash persistence
for WS2812 settings.

## Verification

- Build the Debug preset after each implementation step.
- Inspect the source order to confirm `MX_IWDG_Init()` follows successful DMP
  initialization and precedes `osKernelStart()`.
- On hardware, force a DMP initialization failure and verify the OLED error
  remains visible without reset; restore the connection and verify boot
  continues.
- On hardware, verify each WS2812 page, power-off behavior, brightness at
  0/50/100 percent, every color, and all four effects.
