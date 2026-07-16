$header = Get-Content "$PSScriptRoot/../User/WS2812/ws2812.h" -Raw
$source = Get-Content "$PSScriptRoot/../User/WS2812/ws2812.c" -Raw

if ($header -match 'WS2812_MODE_RAINBOW' -or $source -match 'WS2812_MODE_RAINBOW' -or $source -match 'static WS2812_Rgb_t Hue') {
    throw 'Rainbow must not remain as a WS2812 effect.'
}

foreach ($mode in 'WS2812_MODE_STATIC', 'WS2812_MODE_BREATHE', 'WS2812_MODE_CHASE') {
    if ($header -notmatch $mode -or $source -notmatch $mode) {
        throw "Missing required WS2812 effect: $mode"
    }
}
