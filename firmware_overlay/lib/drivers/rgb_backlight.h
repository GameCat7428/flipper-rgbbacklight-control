#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RGB_BACKLIGHT_LED_COUNT 3

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} RgbBacklightColor;

typedef enum {
    RgbBacklightModeStatic = 0,
    RgbBacklightModeWave,
    RgbBacklightModeSolid,
    RgbBacklightModeCount,
} RgbBacklightMode;

void rgb_backlight_load_settings(void);
void rgb_backlight_save_settings(void);

bool rgb_backlight_is_enabled(void);
void rgb_backlight_set_enabled(bool enabled);

void rgb_backlight_get_color(uint8_t index, RgbBacklightColor* color);
void rgb_backlight_set_color(uint8_t index, const RgbBacklightColor* color);

void rgb_backlight_set_mode(RgbBacklightMode mode);
RgbBacklightMode rgb_backlight_get_mode(void);

void rgb_backlight_set_speed(uint8_t speed);
uint8_t rgb_backlight_get_speed(void);

void rgb_backlight_set_interval(uint32_t interval_ms);
uint32_t rgb_backlight_get_interval(void);

void rgb_backlight_set_saturation(uint8_t saturation);
uint8_t rgb_backlight_get_saturation(void);

void rgb_backlight_update(uint8_t brightness, bool force);

#ifdef __cplusplus
}
#endif
