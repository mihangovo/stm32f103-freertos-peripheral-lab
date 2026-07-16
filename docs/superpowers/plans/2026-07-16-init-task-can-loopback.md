# InitTask and CAN Loopback Implementation Plan

**Goal:** Move blocking startup work into a self-deleting InitTask and add a CAN loopback task using FIFO0 interrupts and a FreeRTOS queue.

## Tasks

- [ ] Add `system_init` with a system-ready event flag. Every business task waits for the flag before its loop.
- [ ] Move UART/I2C/OLED/Flash/MPU/DMP/meta startup from `main.c` into `Init_Task`; start IWDG only after successful initialization, then release tasks and delete InitTask.
- [ ] Add `can_task`: configure FIFO0 filter, start CAN, enqueue frames from the HAL callback, and send/receive a 500 ms loopback frame.
- [ ] Add a CAN status page and include CAN in watchdog supervision.
- [ ] Build, run source-order checks, and verify loopback TX/RX counters on hardware.
