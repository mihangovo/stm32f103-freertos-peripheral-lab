#include "ws2812.h"
#include "tim.h"
#include <string.h>

#define WS2812_LED_COUNT      8U
#define WS2812_BITS_PER_LED   24U
#define WS2812_DMA_LEN        (WS2812_LED_COUNT * WS2812_BITS_PER_LED)
#define WS2812_DUTY_0         25U
#define WS2812_DUTY_1         50U

typedef struct { uint8_t red, green, blue; } WS2812_Rgb_t;

static WS2812_Rgb_t g_pixels[WS2812_LED_COUNT];
static uint16_t g_pwm_buffer[WS2812_DMA_LEN];
static volatile uint8_t g_dma_busy;
static volatile WS2812_Mode_t g_mode = WS2812_MODE_OFF;
static volatile WS2812_Color_t g_color = WS2812_COLOR_RED;
static uint8_t g_frame;

static WS2812_Rgb_t Color_Value(WS2812_Color_t color)
{
    static const WS2812_Rgb_t colors[WS2812_COLOR_COUNT] = {
        {255U, 0U, 0U}, {0U, 255U, 0U}, {0U, 0U, 255U}, {255U, 180U, 0U},
        {0U, 255U, 180U}, {180U, 0U, 255U}, {255U, 255U, 255U}
    };
    return colors[color < WS2812_COLOR_COUNT ? color : WS2812_COLOR_RED];
}

static WS2812_Rgb_t Hue(uint8_t hue)
{
    uint8_t region = hue / 43U;
    uint8_t remainder = (hue - region * 43U) * 6U;
    uint8_t q = 255U - remainder;
    switch(region) {
        case 0U: return (WS2812_Rgb_t){255U, remainder, 0U};
        case 1U: return (WS2812_Rgb_t){q, 255U, 0U};
        case 2U: return (WS2812_Rgb_t){0U, 255U, remainder};
        case 3U: return (WS2812_Rgb_t){0U, q, 255U};
        case 4U: return (WS2812_Rgb_t){remainder, 0U, 255U};
        default: return (WS2812_Rgb_t){255U, 0U, q};
    }
}

static void Encode_Frame(void)
{
    uint16_t out = 0U;
    for(uint8_t i = 0U; i < WS2812_LED_COUNT; ++i) {
        uint8_t bytes[3] = { g_pixels[i].green, g_pixels[i].red, g_pixels[i].blue };
        for(uint8_t byte = 0U; byte < 3U; ++byte) {
            for(uint8_t bit = 0U; bit < 8U; ++bit) {
                g_pwm_buffer[out++] = (bytes[byte] & (0x80U >> bit)) ? WS2812_DUTY_1 : WS2812_DUTY_0;
            }
        }
    }
}

void WS2812_Init(void)
{
    memset(g_pixels, 0, sizeof(g_pixels));
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0U);
}

void WS2812_SelectMode(WS2812_Mode_t mode) { g_mode = (mode < WS2812_MODE_COUNT) ? mode : WS2812_MODE_OFF; }
void WS2812_SelectColor(WS2812_Color_t color) { g_color = (color < WS2812_COLOR_COUNT) ? color : WS2812_COLOR_RED; }
WS2812_Mode_t WS2812_GetMode(void) { return g_mode; }
WS2812_Color_t WS2812_GetColor(void) { return g_color; }

void WS2812_Tick(void)
{
    if(g_dma_busy) return;
    WS2812_Rgb_t color = Color_Value(g_color);
    memset(g_pixels, 0, sizeof(g_pixels));
    if(g_mode == WS2812_MODE_STATIC) {
        for(uint8_t i = 0U; i < WS2812_LED_COUNT; ++i) g_pixels[i] = color;
    } else if(g_mode == WS2812_MODE_RAINBOW) {
        for(uint8_t i = 0U; i < WS2812_LED_COUNT; ++i) g_pixels[i] = Hue((uint8_t)(g_frame + i * 32U));
    } else if(g_mode == WS2812_MODE_CHASE) {
        g_pixels[g_frame % WS2812_LED_COUNT] = color;
    } else if(g_mode == WS2812_MODE_BREATHE) {
        uint8_t level = (g_frame < 128U) ? (uint8_t)(g_frame * 2U) : (uint8_t)((255U - g_frame) * 2U);
        for(uint8_t i = 0U; i < WS2812_LED_COUNT; ++i) {
            g_pixels[i] = (WS2812_Rgb_t){(uint8_t)((uint16_t)color.red * level / 255U), (uint8_t)((uint16_t)color.green * level / 255U), (uint8_t)((uint16_t)color.blue * level / 255U)};
        }
    }
    ++g_frame;
    Encode_Frame();
    g_dma_busy = 1U;
    (void)HAL_TIM_PWM_Start_DMA(&htim3, TIM_CHANNEL_3, (uint32_t *)g_pwm_buffer, WS2812_DMA_LEN);
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if(htim->Instance == TIM3 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_3) {
        (void)HAL_TIM_PWM_Stop_DMA(htim, TIM_CHANNEL_3);
        __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_3, 0U);
        g_dma_busy = 0U;
    }
}
