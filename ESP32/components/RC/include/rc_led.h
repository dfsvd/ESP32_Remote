#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// LED 模式枚举 — 与运行模式一一对应
typedef enum {
    LED_MODE_OFF      = 0,  // 未初始化/关机 (灭)
    LED_MODE_CRSF_RF  = 1,  // 纯射频 CRSF → 红色
    LED_MODE_BLE      = 2,  // BLE HID      → 蓝色
    LED_MODE_USB_HID  = 3,  // USB HID      → 青色
    LED_MODE_USB_XBOX = 4,  // USB Xbox     → 草绿色
    LED_MODE_WIFI     = 5,  // WiFi AP      → 绿色
    LED_MODE_BIND     = 6,  // 自动对频中    → 黄色快闪
    LED_MODE_COUNT
} led_mode_t;

// LED 灯效类型
typedef enum {
    LED_EFFECT_SOLID   = 0,  // 常亮
    LED_EFFECT_BLINK   = 1,  // 闪烁 (interval_ms 可调)
    LED_EFFECT_BREATH  = 2,  // 呼吸 (预留)
    LED_EFFECT_RAINBOW = 3,  // 炫彩 — 色相自动循环
} led_effect_t;

// 单个模式的 LED 配置
typedef struct {
    uint8_t      r, g, b;
    led_effect_t effect;
    uint8_t      brightness;   // 0-4 档 → 20%/40%/60%/80%/100%
    uint16_t     interval_ms;  // 闪烁/炫彩周期 (ms), 默认 500
} led_mode_cfg_t;

// 全局 LED 配置 (7 模式)
typedef struct {
    led_mode_cfg_t modes[LED_MODE_COUNT];
} led_config_t;

void led_init(void);
void led_set_mode(led_mode_t mode);
void led_poll(void);

// 运行时更新（Web 端实时预览）
void led_update_color(led_mode_t mode, uint8_t r, uint8_t g, uint8_t b,
                      led_effect_t effect, uint8_t brightness, uint16_t interval_ms);

const led_config_t* led_get_config(void);
void led_apply_config(const led_config_t *cfg);

#ifdef __cplusplus
}
#endif
