$storage = Get-Content "$PSScriptRoot/../User/TASK/storage_task.h" -Raw
$driver = Get-Content "$PSScriptRoot/../User/WS2812/ws2812.c" -Raw
$ledTask = Get-Content "$PSScriptRoot/../User/TASK/led_task.c" -Raw
$uiTask = Get-Content "$PSScriptRoot/../User/TASK/ui_task.c" -Raw

foreach ($field in 'ws2812_brightness', 'ws2812_color', 'ws2812_mode_power') {
    if ($storage -notmatch $field) { throw "Missing persistent WS2812 field: $field" }
}

if ($driver -notmatch 'WS2812_CHASE_STEP_TICKS\s+10U') { throw 'Chase interval must be 200 ms.' }
if ($driver -notmatch 'WS2812_BREATHE_STEP_TICKS\s+6U') { throw 'Breathe interval must be 120 ms.' }
if ($ledTask -notmatch 'WS2812_SetBrightness' -or $ledTask -notmatch 'WS2812_SetPower') { throw 'LED task must restore WS2812 settings.' }
if ($uiTask -notmatch 'Storage_RequestSaveState\(\)') { throw 'UI must request persistent state save.' }
