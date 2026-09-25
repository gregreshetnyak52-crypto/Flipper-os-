#include "settings.h"

#include <furi.h>
#include <storage/storage.h>
#include <toolbox/saved_struct.h>

#define FLIPPER_OS_SETTINGS_PATH APP_DATA_PATH("settings.dat")
#define FLIPPER_OS_SETTINGS_MAGIC 0x05
#define FLIPPER_OS_SETTINGS_VERSION 2

// Layout written by Toolkit 1.0-1.2
typedef struct {
    uint16_t snake_best;
    int16_t counter_value;
    int16_t counter_step;
    uint8_t dice_sides_index;
    uint8_t dice_count;
    uint8_t morse_message;
    uint8_t morse_output;
    bool morse_loop;
    uint8_t flashlight_level;
    uint8_t flashlight_mode;
    uint16_t timer_seconds;
} FlipperOsSettingsV1;

void flipper_os_settings_defaults(FlipperOsSettings* settings) {
    memset(settings, 0, sizeof(FlipperOsSettings));
    settings->counter_step = 1;
    settings->dice_sides_index = 1; // d6
    settings->dice_count = 1;
    settings->flashlight_level = 3; // 100%
    settings->timer_seconds = 5 * 60;
    strlcpy(settings->morse_text, "FLIPPER OS", sizeof(settings->morse_text));
    settings->pomodoro_minutes[0] = 25;
    settings->pomodoro_minutes[1] = 5;
    settings->pomodoro_minutes[2] = 15;
    settings->password_length = 16;
    settings->password_charset = 2; // letters and digits
}

static bool flipper_os_settings_load_v1(FlipperOsSettings* settings) {
    FlipperOsSettingsV1 old;
    if(!saved_struct_load(
           FLIPPER_OS_SETTINGS_PATH, &old, sizeof(old), FLIPPER_OS_SETTINGS_MAGIC, 1)) {
        return false;
    }
    // Fields added later keep their defaults
    flipper_os_settings_defaults(settings);
    settings->snake_best = old.snake_best;
    settings->counter_value = old.counter_value;
    settings->counter_step = old.counter_step;
    settings->dice_sides_index = old.dice_sides_index;
    settings->dice_count = old.dice_count;
    settings->morse_message = old.morse_message;
    settings->morse_output = old.morse_output;
    settings->morse_loop = old.morse_loop;
    settings->flashlight_level = old.flashlight_level;
    settings->flashlight_mode = old.flashlight_mode;
    settings->timer_seconds = old.timer_seconds;
    return true;
}

void flipper_os_settings_load(FlipperOsSettings* settings) {
    if(saved_struct_load(
           FLIPPER_OS_SETTINGS_PATH,
           settings,
           sizeof(FlipperOsSettings),
           FLIPPER_OS_SETTINGS_MAGIC,
           FLIPPER_OS_SETTINGS_VERSION)) {
        // The text is shown and encoded as a C string: never trust the file
        settings->morse_text[FLIPPER_OS_MORSE_TEXT_LEN] = '\0';
        return;
    }
    if(!flipper_os_settings_load_v1(settings)) {
        flipper_os_settings_defaults(settings);
    }
}

void flipper_os_settings_save(const FlipperOsSettings* settings) {
    if(!saved_struct_save(
           FLIPPER_OS_SETTINGS_PATH,
           settings,
           sizeof(FlipperOsSettings),
           FLIPPER_OS_SETTINGS_MAGIC,
           FLIPPER_OS_SETTINGS_VERSION)) {
        FURI_LOG_E("FlipperOS", "Failed to save settings");
    }
}
