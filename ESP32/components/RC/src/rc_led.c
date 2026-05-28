#include "rc_led.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "led_strip_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "LED";

#define WS2812_GPIO      48
#define WS2812_NUM       1
#define BREATH_LUT_SIZE  256

static led_strip_handle_t s_strip = NULL;
static led_mode_t         s_mode  = LED_MODE_OFF;
static uint32_t s_mode_since_ms = 0;
static bool     s_blink_on      = false;
static uint16_t s_rainbow_hue   = 0;
static uint16_t s_breath_idx    = 0;
static uint8_t  s_breath_lut[BREATH_LUT_SIZE];

// 默认配置 (NVS 无数据时使用)
static const led_mode_cfg_t s_defaults[LED_MODE_COUNT] = {
    [LED_MODE_OFF]      = {0,   0,   0,   LED_EFFECT_SOLID,   4, 500},
    [LED_MODE_CRSF_RF]  = {255, 0,   0,   LED_EFFECT_SOLID,   4, 500},
    [LED_MODE_BLE]      = {0,   0,   255, LED_EFFECT_SOLID,   4, 500},
    [LED_MODE_USB_HID]  = {0,   255, 255, LED_EFFECT_SOLID,   4, 500},
    [LED_MODE_USB_XBOX] = {0,   255, 0,   LED_EFFECT_SOLID,   4, 500},
    [LED_MODE_WIFI]     = {0,   255, 0,   LED_EFFECT_SOLID,   4, 500},
    [LED_MODE_BIND]     = {255, 255, 0,   LED_EFFECT_BLINK,   4, 500},
};

// 运行时配置表
static led_config_t s_cfg;

static uint32_t _brightness_scale(uint8_t val, uint8_t brightness)
{
    return ((uint32_t)val * (brightness + 1)) / 5;
}

static void _set_strip(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) return;
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

// 预计算呼吸灯指数曲线 LUT
// 公式: D(x) = e^((x - 1) * k) 补偿人眼对数感知 (韦伯-费希纳定律)
// 非对称: 吸气(变亮) 40% / 呼气(变暗) 60%
static void _init_breath_lut(void)
{
    const float k = 4.0f;
    const float exp_minus_k = expf(-k);
    const float scale = 1.0f / (1.0f - exp_minus_k);
    int rise_len = BREATH_LUT_SIZE * 40 / 100;
    if (rise_len < 1) rise_len = 1;
    int fall_len = BREATH_LUT_SIZE - rise_len;

    for (int i = 0; i < rise_len; i++) {
        float t = (float)i / (rise_len - 1);
        float val = (expf((t - 1.0f) * k) - exp_minus_k) * scale;
        s_breath_lut[i] = (uint8_t)(val * 255.0f);
    }
    for (int i = 0; i < fall_len; i++) {
        float t = 1.0f - (float)i / (fall_len - 1);
        float val = (expf((t - 1.0f) * k) - exp_minus_k) * scale;
        s_breath_lut[rise_len + i] = (uint8_t)(val * 255.0f);
    }
}

// 对当前模式生效（实时预览）
static void _apply_current_mode(void)
{
    if (!s_strip || s_mode >= LED_MODE_COUNT)
        return;

    led_mode_cfg_t *c = &s_cfg.modes[s_mode];

    // 闪烁/炫彩/呼吸 由 led_poll 控制
    if (c->effect == LED_EFFECT_BLINK || c->effect == LED_EFFECT_RAINBOW || c->effect == LED_EFFECT_BREATH) {
        s_blink_on = false;
        return;
    }

    uint8_t r = (uint8_t)_brightness_scale(c->r, c->brightness);
    uint8_t g = (uint8_t)_brightness_scale(c->g, c->brightness);
    uint8_t b = (uint8_t)_brightness_scale(c->b, c->brightness);
    _set_strip(r, g, b);
}

void led_init(void)
{
    memcpy(&s_cfg, s_defaults, sizeof(s_cfg));

    led_strip_config_t strip_cfg = {
        .strip_gpio_num   = WS2812_GPIO,
        .max_leds         = WS2812_NUM,
        .led_model        = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_cfg = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags.with_dma    = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip));
    led_strip_clear(s_strip);
    _init_breath_lut();
    ESP_LOGI(TAG, "WS2812 初始化完成, GPIO=%d", WS2812_GPIO);
}

void led_set_mode(led_mode_t mode)
{
    s_mode          = mode;
    s_mode_since_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    s_rainbow_hue   = 0;
    s_breath_idx    = 0;

    if (mode >= LED_MODE_COUNT)
        return;

    _apply_current_mode();

    led_mode_cfg_t *c = &s_cfg.modes[mode];
    ESP_LOGI(TAG, "模式切换: %d → RGB(%d,%d,%d) effect=%d br=%d interval=%d",
             mode, c->r, c->g, c->b, c->effect, c->brightness, c->interval_ms);
}

void led_poll(void)
{
    if (!s_strip || s_mode >= LED_MODE_COUNT)
        return;

    led_mode_cfg_t *c = &s_cfg.modes[s_mode];
    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    if (c->effect == LED_EFFECT_BLINK) {
        uint16_t half = c->interval_ms ? c->interval_ms : 500;
        bool want_on = (((now - s_mode_since_ms) / half) % 2) == 0;
        if (want_on == s_blink_on) return;
        s_blink_on = want_on;
        if (want_on) {
            uint8_t r = (uint8_t)_brightness_scale(c->r, c->brightness);
            uint8_t g = (uint8_t)_brightness_scale(c->g, c->brightness);
            uint8_t b = (uint8_t)_brightness_scale(c->b, c->brightness);
            _set_strip(r, g, b);
        } else {
            _set_strip(0, 0, 0);
        }
        return;
    }

    if (c->effect == LED_EFFECT_RAINBOW) {
        uint16_t period = c->interval_ms ? c->interval_ms : 500;
        uint32_t elapsed = now - s_mode_since_ms;
        uint16_t hue = (uint16_t)((elapsed % period) * 360UL / period);
        if (hue == s_rainbow_hue) return;
        s_rainbow_hue = hue;

        // HSV → RGB (full saturation, full value)
        uint8_t r, g, b;
        uint8_t br = _brightness_scale(255, c->brightness);
        uint8_t seg = (hue / 60) % 6;
        uint8_t step = hue % 60;
        uint8_t rise = (uint8_t)((uint32_t)br * step / 60);
        uint8_t fall = (uint8_t)((uint32_t)br * (60 - step) / 60);
        switch (seg) {
            case 0: r = br;  g = rise; b = 0;   break;
            case 1: r = fall; g = br;   b = 0;   break;
            case 2: r = 0;   g = br;   b = rise; break;
            case 3: r = 0;   g = fall; b = br;   break;
            case 4: r = rise; g = 0;   b = br;   break;
            case 5: r = br;  g = 0;   b = fall; break;
            default: r = br; g = 0;   b = 0;     break;
        }
        _set_strip(r, g, b);
        return;
    }

    if (c->effect == LED_EFFECT_BREATH) {
        uint16_t period = c->interval_ms ? c->interval_ms : 3000;
        uint32_t elapsed = now - s_mode_since_ms;
        uint16_t idx = (uint16_t)((elapsed % period) * BREATH_LUT_SIZE / period);
        if (idx == s_breath_idx) return;
        s_breath_idx = idx;
        if (idx >= BREATH_LUT_SIZE) idx = BREATH_LUT_SIZE - 1;
        // 指数曲线亮度 × 用户亮度档位
        uint8_t breath_br = (uint8_t)_brightness_scale(s_breath_lut[idx], c->brightness);
        uint8_t r = (uint8_t)((uint32_t)c->r * breath_br / 255);
        uint8_t g = (uint8_t)((uint32_t)c->g * breath_br / 255);
        uint8_t b = (uint8_t)((uint32_t)c->b * breath_br / 255);
        _set_strip(r, g, b);
        return;
    }
}

void led_update_color(led_mode_t mode, uint8_t r, uint8_t g, uint8_t b,
                      led_effect_t effect, uint8_t brightness, uint16_t interval_ms)
{
    if (mode >= LED_MODE_COUNT)
        return;

    s_cfg.modes[mode].r          = r;
    s_cfg.modes[mode].g          = g;
    s_cfg.modes[mode].b          = b;
    s_cfg.modes[mode].effect     = effect;
    s_cfg.modes[mode].brightness = brightness > 4 ? 4 : brightness;
    s_cfg.modes[mode].interval_ms = interval_ms;

    if (mode == s_mode && s_strip) {
        s_mode_since_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        s_rainbow_hue   = 0;
        s_breath_idx    = 0;
        _apply_current_mode();
    }

    ESP_LOGI(TAG, "更新 mode=%d RGB(%d,%d,%d) effect=%d br=%d interval=%u",
             mode, r, g, b, effect, brightness, interval_ms);
}

const led_config_t* led_get_config(void)
{
    return &s_cfg;
}

void led_apply_config(const led_config_t *cfg)
{
    if (!cfg) return;
    memcpy(&s_cfg, cfg, sizeof(s_cfg));
    if (s_strip && s_mode < LED_MODE_COUNT) {
        s_mode_since_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        s_rainbow_hue   = 0;
        s_breath_idx    = 0;
        _apply_current_mode();
    }
}
