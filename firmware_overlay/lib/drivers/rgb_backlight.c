#include "rgb_backlight.h"
#include "rgb_backlight_filename.h"

#include <furi.h>
#include <furi_hal.h>
#include <toolbox/saved_struct.h>

#define RGB_BACKLIGHT_SETTINGS_MAGIC 0x52
#define RGB_BACKLIGHT_SETTINGS_VERSION 0x01

#define RGB_BACKLIGHT_DEFAULT_SPEED 5u
#define RGB_BACKLIGHT_MAX_SPEED 25u
#define RGB_BACKLIGHT_MIN_INTERVAL_MS 100u
#define RGB_BACKLIGHT_MAX_INTERVAL_MS 5000u

#define RGB_BACKLIGHT_T1H_CYCLES 30u
#define RGB_BACKLIGHT_T1L_CYCLES 26u
#define RGB_BACKLIGHT_T0H_CYCLES 11u
#define RGB_BACKLIGHT_T0L_CYCLES 43u

static const GpioPin rgb_backlight_pin = {.port = GPIOA, .pin = LL_GPIO_PIN_8};

typedef struct {
    uint8_t enabled;
    uint8_t mode;
    uint8_t speed;
    uint8_t saturation;
    uint32_t interval_ms;
    RgbBacklightColor colors[RGB_BACKLIGHT_LED_COUNT];
} RgbBacklightSettings;

static struct {
    FuriMutex* mutex;
    FuriTimer* rainbow_timer;
    bool settings_loaded;
    uint8_t last_brightness;
    uint8_t hue;
    uint8_t led_frame[RGB_BACKLIGHT_LED_COUNT][3];
    RgbBacklightSettings settings;
} rgb_backlight = {
    .settings =
        {
            .enabled = 0,
            .mode = RgbBacklightModeStatic,
            .speed = RGB_BACKLIGHT_DEFAULT_SPEED,
            .saturation = 255,
            .interval_ms = 250,
            .colors = {
                {.r = 255, .g = 69, .b = 0},
                {.r = 255, .g = 69, .b = 0},
                {.r = 255, .g = 69, .b = 0},
            },
        },
};

static void rgb_backlight_apply_locked(bool force);

static void rgb_backlight_ensure_ready(void) {
    if(!rgb_backlight.mutex) {
        rgb_backlight.mutex = furi_mutex_alloc(FuriMutexTypeRecursive);
    }
}

static void rgb_backlight_wait_cycles(uint32_t cycles) {
    uint32_t end = DWT->CYCCNT + cycles;
    while((int32_t)(DWT->CYCCNT - end) < 0) {
    }
}

static void rgb_backlight_set_frame_color(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    rgb_backlight.led_frame[index][0] = g;
    rgb_backlight.led_frame[index][1] = r;
    rgb_backlight.led_frame[index][2] = b;
}

static void rgb_backlight_write_frame(void) {
    furi_hal_gpio_write(&rgb_backlight_pin, false);
    furi_hal_gpio_init(
        &rgb_backlight_pin, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);

    FURI_CRITICAL_ENTER();
    for(uint8_t led = 0; led < RGB_BACKLIGHT_LED_COUNT; led++) {
        for(uint8_t channel = 0; channel < 3; channel++) {
            for(uint8_t mask = 0x80; mask != 0; mask >>= 1) {
                const bool bit_is_set = (rgb_backlight.led_frame[led][channel] & mask) != 0;
                furi_hal_gpio_write(&rgb_backlight_pin, true);
                rgb_backlight_wait_cycles(
                    bit_is_set ? RGB_BACKLIGHT_T1H_CYCLES : RGB_BACKLIGHT_T0H_CYCLES);
                furi_hal_gpio_write(&rgb_backlight_pin, false);
                rgb_backlight_wait_cycles(
                    bit_is_set ? RGB_BACKLIGHT_T1L_CYCLES : RGB_BACKLIGHT_T0L_CYCLES);
            }
        }
    }
    FURI_CRITICAL_EXIT();

    furi_delay_us(80);
}

static void rgb_backlight_clear_output_locked(void) {
    for(uint8_t led = 0; led < RGB_BACKLIGHT_LED_COUNT; led++) {
        rgb_backlight_set_frame_color(led, 0, 0, 0);
    }
    rgb_backlight_write_frame();
}

static uint8_t rgb_backlight_scale_channel(uint8_t channel, uint8_t brightness) {
    return ((uint16_t)channel * brightness) / 255u;
}

static RgbBacklightColor rgb_backlight_hsv_to_rgb(uint8_t hue, uint8_t saturation, uint8_t value) {
    RgbBacklightColor color = {.r = value, .g = value, .b = value};
    if(saturation == 0) {
        return color;
    }

    const uint8_t region = hue / 43u;
    const uint8_t remainder = (hue - (region * 43u)) * 6u;

    const uint8_t p = ((uint16_t)value * (255u - saturation)) >> 8;
    const uint8_t q =
        ((uint16_t)value * (255u - (((uint16_t)saturation * remainder) >> 8))) >> 8;
    const uint8_t t =
        ((uint16_t)value * (255u - (((uint16_t)saturation * (255u - remainder)) >> 8))) >> 8;

    switch(region) {
    case 0:
        color.r = value;
        color.g = t;
        color.b = p;
        break;
    case 1:
        color.r = q;
        color.g = value;
        color.b = p;
        break;
    case 2:
        color.r = p;
        color.g = value;
        color.b = t;
        break;
    case 3:
        color.r = p;
        color.g = q;
        color.b = value;
        break;
    case 4:
        color.r = t;
        color.g = p;
        color.b = value;
        break;
    default:
        color.r = value;
        color.g = p;
        color.b = q;
        break;
    }

    return color;
}

static void rgb_backlight_validate_locked(void) {
    rgb_backlight.settings.enabled = rgb_backlight.settings.enabled ? 1u : 0u;

    if(rgb_backlight.settings.mode >= RgbBacklightModeCount) {
        rgb_backlight.settings.mode = RgbBacklightModeStatic;
    }

    if(rgb_backlight.settings.speed == 0) {
        rgb_backlight.settings.speed = 1;
    } else if(rgb_backlight.settings.speed > RGB_BACKLIGHT_MAX_SPEED) {
        rgb_backlight.settings.speed = RGB_BACKLIGHT_MAX_SPEED;
    }

    if(rgb_backlight.settings.saturation == 0) {
        rgb_backlight.settings.saturation = 1;
    }

    if(rgb_backlight.settings.interval_ms < RGB_BACKLIGHT_MIN_INTERVAL_MS) {
        rgb_backlight.settings.interval_ms = RGB_BACKLIGHT_MIN_INTERVAL_MS;
    } else if(rgb_backlight.settings.interval_ms > RGB_BACKLIGHT_MAX_INTERVAL_MS) {
        rgb_backlight.settings.interval_ms = RGB_BACKLIGHT_MAX_INTERVAL_MS;
    }
}

static void rgb_backlight_render_locked(void) {
    if(!rgb_backlight.settings.enabled || !rgb_backlight.last_brightness) {
        rgb_backlight_clear_output_locked();
        return;
    }

    if(rgb_backlight.settings.mode == RgbBacklightModeStatic) {
        for(uint8_t led = 0; led < RGB_BACKLIGHT_LED_COUNT; led++) {
            const RgbBacklightColor* color = &rgb_backlight.settings.colors[led];
            rgb_backlight_set_frame_color(
                led,
                rgb_backlight_scale_channel(color->r, rgb_backlight.last_brightness),
                rgb_backlight_scale_channel(color->g, rgb_backlight.last_brightness),
                rgb_backlight_scale_channel(color->b, rgb_backlight.last_brightness));
        }
    } else {
        for(uint8_t led = 0; led < RGB_BACKLIGHT_LED_COUNT; led++) {
            const uint8_t hue =
                (rgb_backlight.settings.mode == RgbBacklightModeWave) ?
                    (uint8_t)(rgb_backlight.hue + (50u * led)) :
                    rgb_backlight.hue;
            const RgbBacklightColor color = rgb_backlight_hsv_to_rgb(
                hue, rgb_backlight.settings.saturation, rgb_backlight.last_brightness);
            rgb_backlight_set_frame_color(led, color.r, color.g, color.b);
        }
    }

    rgb_backlight_write_frame();
}

static void rgb_backlight_rainbow_timer_callback(void* context) {
    UNUSED(context);

    rgb_backlight_ensure_ready();
    if(furi_mutex_acquire(rgb_backlight.mutex, FuriWaitForever) != FuriStatusOk) {
        return;
    }

    if(!rgb_backlight.settings_loaded || !rgb_backlight.settings.enabled ||
       !rgb_backlight.last_brightness ||
       (rgb_backlight.settings.mode == RgbBacklightModeStatic)) {
        furi_mutex_release(rgb_backlight.mutex);
        return;
    }

    rgb_backlight.hue += rgb_backlight.settings.speed;
    rgb_backlight_render_locked();

    furi_mutex_release(rgb_backlight.mutex);
}

static void rgb_backlight_apply_locked(bool force) {
    UNUSED(force);

    const bool rainbow_active =
        rgb_backlight.settings.enabled && rgb_backlight.last_brightness &&
        (rgb_backlight.settings.mode != RgbBacklightModeStatic);

    if(rainbow_active) {
        if(!rgb_backlight.rainbow_timer) {
            rgb_backlight.rainbow_timer =
                furi_timer_alloc(rgb_backlight_rainbow_timer_callback, FuriTimerTypePeriodic, NULL);
        }
        furi_timer_start(rgb_backlight.rainbow_timer, rgb_backlight.settings.interval_ms);
    } else if(rgb_backlight.rainbow_timer) {
        furi_timer_stop(rgb_backlight.rainbow_timer);
    }

    rgb_backlight_render_locked();
}

void rgb_backlight_load_settings(void) {
    rgb_backlight_ensure_ready();
    furi_check(furi_mutex_acquire(rgb_backlight.mutex, FuriWaitForever) == FuriStatusOk);

    rgb_backlight.settings = (RgbBacklightSettings){
        .enabled = 0,
        .mode = RgbBacklightModeStatic,
        .speed = RGB_BACKLIGHT_DEFAULT_SPEED,
        .saturation = 255,
        .interval_ms = 250,
        .colors = {
            {.r = 255, .g = 69, .b = 0},
            {.r = 255, .g = 69, .b = 0},
            {.r = 255, .g = 69, .b = 0},
        },
    };

    if(furi_hal_is_normal_boot()) {
        saved_struct_load(
            RGB_BACKLIGHT_SETTINGS_PATH,
            &rgb_backlight.settings,
            sizeof(rgb_backlight.settings),
            RGB_BACKLIGHT_SETTINGS_MAGIC,
            RGB_BACKLIGHT_SETTINGS_VERSION);
    }

    rgb_backlight_validate_locked();
    rgb_backlight.settings_loaded = true;
    rgb_backlight_apply_locked(true);

    furi_check(furi_mutex_release(rgb_backlight.mutex) == FuriStatusOk);
}

void rgb_backlight_save_settings(void) {
    rgb_backlight_ensure_ready();
    furi_check(furi_mutex_acquire(rgb_backlight.mutex, FuriWaitForever) == FuriStatusOk);

    if(rgb_backlight.settings_loaded) {
        saved_struct_save(
            RGB_BACKLIGHT_SETTINGS_PATH,
            &rgb_backlight.settings,
            sizeof(rgb_backlight.settings),
            RGB_BACKLIGHT_SETTINGS_MAGIC,
            RGB_BACKLIGHT_SETTINGS_VERSION);
    }

    furi_check(furi_mutex_release(rgb_backlight.mutex) == FuriStatusOk);
}

bool rgb_backlight_is_enabled(void) {
    bool enabled;

    rgb_backlight_ensure_ready();
    furi_check(furi_mutex_acquire(rgb_backlight.mutex, FuriWaitForever) == FuriStatusOk);
    enabled = rgb_backlight.settings.enabled;
    furi_check(furi_mutex_release(rgb_backlight.mutex) == FuriStatusOk);

    return enabled;
}

void rgb_backlight_set_enabled(bool enabled) {
    rgb_backlight_ensure_ready();
    furi_check(furi_mutex_acquire(rgb_backlight.mutex, FuriWaitForever) == FuriStatusOk);

    rgb_backlight.settings.enabled = enabled ? 1u : 0u;
    rgb_backlight_validate_locked();
    if(rgb_backlight.settings_loaded) {
        rgb_backlight_apply_locked(true);
    }

    furi_check(furi_mutex_release(rgb_backlight.mutex) == FuriStatusOk);
}

void rgb_backlight_get_color(uint8_t index, RgbBacklightColor* color) {
    if((index >= RGB_BACKLIGHT_LED_COUNT) || !color) {
        return;
    }

    rgb_backlight_ensure_ready();
    furi_check(furi_mutex_acquire(rgb_backlight.mutex, FuriWaitForever) == FuriStatusOk);
    *color = rgb_backlight.settings.colors[index];
    furi_check(furi_mutex_release(rgb_backlight.mutex) == FuriStatusOk);
}

void rgb_backlight_set_color(uint8_t index, const RgbBacklightColor* color) {
    if((index >= RGB_BACKLIGHT_LED_COUNT) || !color) {
        return;
    }

    rgb_backlight_ensure_ready();
    furi_check(furi_mutex_acquire(rgb_backlight.mutex, FuriWaitForever) == FuriStatusOk);

    rgb_backlight.settings.colors[index] = *color;
    if(rgb_backlight.settings_loaded) {
        rgb_backlight_apply_locked(true);
    }

    furi_check(furi_mutex_release(rgb_backlight.mutex) == FuriStatusOk);
}

void rgb_backlight_set_mode(RgbBacklightMode mode) {
    if(mode >= RgbBacklightModeCount) {
        return;
    }

    rgb_backlight_ensure_ready();
    furi_check(furi_mutex_acquire(rgb_backlight.mutex, FuriWaitForever) == FuriStatusOk);

    rgb_backlight.settings.mode = mode;
    if(rgb_backlight.settings_loaded) {
        rgb_backlight_apply_locked(true);
    }

    furi_check(furi_mutex_release(rgb_backlight.mutex) == FuriStatusOk);
}

RgbBacklightMode rgb_backlight_get_mode(void) {
    RgbBacklightMode mode;

    rgb_backlight_ensure_ready();
    furi_check(furi_mutex_acquire(rgb_backlight.mutex, FuriWaitForever) == FuriStatusOk);
    mode = (RgbBacklightMode)rgb_backlight.settings.mode;
    furi_check(furi_mutex_release(rgb_backlight.mutex) == FuriStatusOk);

    return mode;
}

void rgb_backlight_set_speed(uint8_t speed) {
    rgb_backlight_ensure_ready();
    furi_check(furi_mutex_acquire(rgb_backlight.mutex, FuriWaitForever) == FuriStatusOk);

    rgb_backlight.settings.speed = speed;
    rgb_backlight_validate_locked();

    furi_check(furi_mutex_release(rgb_backlight.mutex) == FuriStatusOk);
}

uint8_t rgb_backlight_get_speed(void) {
    uint8_t speed;

    rgb_backlight_ensure_ready();
    furi_check(furi_mutex_acquire(rgb_backlight.mutex, FuriWaitForever) == FuriStatusOk);
    speed = rgb_backlight.settings.speed;
    furi_check(furi_mutex_release(rgb_backlight.mutex) == FuriStatusOk);

    return speed;
}

void rgb_backlight_set_interval(uint32_t interval_ms) {
    rgb_backlight_ensure_ready();
    furi_check(furi_mutex_acquire(rgb_backlight.mutex, FuriWaitForever) == FuriStatusOk);

    rgb_backlight.settings.interval_ms = interval_ms;
    rgb_backlight_validate_locked();
    if(rgb_backlight.settings_loaded) {
        rgb_backlight_apply_locked(true);
    }

    furi_check(furi_mutex_release(rgb_backlight.mutex) == FuriStatusOk);
}

uint32_t rgb_backlight_get_interval(void) {
    uint32_t interval_ms;

    rgb_backlight_ensure_ready();
    furi_check(furi_mutex_acquire(rgb_backlight.mutex, FuriWaitForever) == FuriStatusOk);
    interval_ms = rgb_backlight.settings.interval_ms;
    furi_check(furi_mutex_release(rgb_backlight.mutex) == FuriStatusOk);

    return interval_ms;
}

void rgb_backlight_set_saturation(uint8_t saturation) {
    rgb_backlight_ensure_ready();
    furi_check(furi_mutex_acquire(rgb_backlight.mutex, FuriWaitForever) == FuriStatusOk);

    rgb_backlight.settings.saturation = saturation;
    rgb_backlight_validate_locked();
    if(rgb_backlight.settings_loaded) {
        rgb_backlight_apply_locked(true);
    }

    furi_check(furi_mutex_release(rgb_backlight.mutex) == FuriStatusOk);
}

uint8_t rgb_backlight_get_saturation(void) {
    uint8_t saturation;

    rgb_backlight_ensure_ready();
    furi_check(furi_mutex_acquire(rgb_backlight.mutex, FuriWaitForever) == FuriStatusOk);
    saturation = rgb_backlight.settings.saturation;
    furi_check(furi_mutex_release(rgb_backlight.mutex) == FuriStatusOk);

    return saturation;
}

void rgb_backlight_update(uint8_t brightness, bool force) {
    rgb_backlight_ensure_ready();
    furi_check(furi_mutex_acquire(rgb_backlight.mutex, FuriWaitForever) == FuriStatusOk);

    const uint8_t previous_brightness = rgb_backlight.last_brightness;
    rgb_backlight.last_brightness = brightness;

    if(!rgb_backlight.settings_loaded) {
        furi_check(furi_mutex_release(rgb_backlight.mutex) == FuriStatusOk);
        return;
    }

    if(!rgb_backlight.settings.enabled) {
        furi_check(furi_mutex_release(rgb_backlight.mutex) == FuriStatusOk);
        return;
    }

    if(!force && (previous_brightness == brightness)) {
        furi_check(furi_mutex_release(rgb_backlight.mutex) == FuriStatusOk);
        return;
    }

    rgb_backlight_apply_locked(force);
    furi_check(furi_mutex_release(rgb_backlight.mutex) == FuriStatusOk);
}
