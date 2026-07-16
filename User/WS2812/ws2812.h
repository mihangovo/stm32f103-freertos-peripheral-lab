#ifndef __WS2812_H
#define __WS2812_H

#include <stdint.h>

#define WS2812_META_MODE_MASK   0x0FU
#define WS2812_META_VALID       0x40U
#define WS2812_META_POWER       0x80U

typedef enum {
    WS2812_MODE_STATIC = 0,
    WS2812_MODE_BREATHE,
    WS2812_MODE_CHASE,
    WS2812_MODE_COUNT
} WS2812_Mode_t;

typedef enum {
    WS2812_COLOR_RED = 0,
    WS2812_COLOR_GREEN,
    WS2812_COLOR_BLUE,
    WS2812_COLOR_YELLOW,
    WS2812_COLOR_CYAN,
    WS2812_COLOR_PURPLE,
    WS2812_COLOR_WHITE,
    WS2812_COLOR_COUNT
} WS2812_Color_t;

void WS2812_Init(void);
void WS2812_SetPower(uint8_t on);
uint8_t WS2812_GetPower(void);
void WS2812_SetBrightness(uint8_t brightness);
uint8_t WS2812_GetBrightness(void);
void WS2812_SelectMode(WS2812_Mode_t mode);
void WS2812_SelectColor(WS2812_Color_t color);
WS2812_Mode_t WS2812_GetMode(void);
WS2812_Color_t WS2812_GetColor(void);
void WS2812_Tick(void);

#endif
