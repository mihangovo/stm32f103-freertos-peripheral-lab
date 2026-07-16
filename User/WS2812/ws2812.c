#include "ws2812.h"
#include "tim.h"
#include <string.h>

#define WS2812_LED_COUNT      8U
#define WS2812_BITS_PER_LED   24U
#define WS2812_DMA_LEN        (WS2812_LED_COUNT * WS2812_BITS_PER_LED)
#define WS2812_DUTY_0         25U
#define WS2812_DUTY_1         50U
#define WS2812_CHASE_STEP_TICKS   10U
#define WS2812_BREATHE_STEP_TICKS 6U

typedef struct { uint8_t red, green, blue; } WS2812_Rgb_t;

static WS2812_Rgb_t g_pixels[WS2812_LED_COUNT];
static uint16_t g_pwm_buffer[WS2812_DMA_LEN];
static volatile uint8_t g_dma_busy;
static volatile uint8_t g_power = 0U;
static volatile uint8_t g_brightness = 100U;
static volatile WS2812_Mode_t g_mode = WS2812_MODE_STATIC;
static volatile WS2812_Color_t g_color = WS2812_COLOR_RED;
static uint8_t g_frame;
static uint8_t g_chase_tick;
static uint8_t g_breathe_tick;

static WS2812_Rgb_t Color_Value(WS2812_Color_t color)
{
    static const WS2812_Rgb_t colors[WS2812_COLOR_COUNT] = {
        {255U, 0U, 0U}, {0U, 255U, 0U}, {0U, 0U, 255U}, {255U, 180U, 0U},
        {0U, 255U, 180U}, {180U, 0U, 255U}, {255U, 255U, 255U}
    };
    return colors[color < WS2812_COLOR_COUNT ? color : WS2812_COLOR_RED];
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
    g_frame = 0U;
    g_chase_tick = 0U;
    g_breathe_tick = 0U;
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0U);
}

void WS2812_SetPower(uint8_t on) { g_power = (on != 0U) ? 1U : 0U; }
uint8_t WS2812_GetPower(void) { return g_power; }

void WS2812_SetBrightness(uint8_t brightness)
{
    g_brightness = (brightness > 100U) ? 100U : brightness;
}

uint8_t WS2812_GetBrightness(void) { return g_brightness; }

void WS2812_SelectMode(WS2812_Mode_t mode) { g_mode = (mode < WS2812_MODE_COUNT) ? mode : WS2812_MODE_STATIC; }
void WS2812_SelectColor(WS2812_Color_t color) { g_color = (color < WS2812_COLOR_COUNT) ? color : WS2812_COLOR_RED; }
WS2812_Mode_t WS2812_GetMode(void) { return g_mode; }
WS2812_Color_t WS2812_GetColor(void) { return g_color; }

void WS2812_Tick(void)
{
    if(g_dma_busy) return;
    WS2812_Rgb_t color = Color_Value(g_color);
    memset(g_pixels, 0, sizeof(g_pixels));
    if(g_power != 0U && g_mode == WS2812_MODE_STATIC) {
        for(uint8_t i = 0U; i < WS2812_LED_COUNT; ++i) g_pixels[i] = color;
    } else if(g_power != 0U && g_mode == WS2812_MODE_BREATHE) {
        if(++g_breathe_tick >= WS2812_BREATHE_STEP_TICKS) {
            g_breathe_tick = 0U;
            g_frame = (uint8_t)(g_frame + 4U);
        }
        uint8_t level = (g_frame < 128U) ? (uint8_t)(g_frame * 2U) : (uint8_t)((255U - g_frame) * 2U);
        for(uint8_t i = 0U; i < WS2812_LED_COUNT; ++i) {
            g_pixels[i] = (WS2812_Rgb_t){(uint8_t)((uint16_t)color.red * level / 255U), (uint8_t)((uint16_t)color.green * level / 255U), (uint8_t)((uint16_t)color.blue * level / 255U)};
        }
    } else if(g_power != 0U && g_mode == WS2812_MODE_CHASE) {
        if(++g_chase_tick >= WS2812_CHASE_STEP_TICKS) {
            g_chase_tick = 0U;
            ++g_frame;
        }
        g_pixels[g_frame % WS2812_LED_COUNT] = color;
    }

    for(uint8_t i = 0U; i < WS2812_LED_COUNT; ++i) {
        g_pixels[i].red = (uint8_t)((uint16_t)g_pixels[i].red * g_brightness / 100U);
        g_pixels[i].green = (uint8_t)((uint16_t)g_pixels[i].green * g_brightness / 100U);
        g_pixels[i].blue = (uint8_t)((uint16_t)g_pixels[i].blue * g_brightness / 100U);
    }

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
