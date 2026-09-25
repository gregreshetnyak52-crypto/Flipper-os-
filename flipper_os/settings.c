#include "settings.h"

#include <furi.h>
#include <storage/storage.h>
#include <toolbox/saved_struct.h>

#define FLIPPER_OS_SETTINGS_PATH APP_DATA_PATH("settings.dat")
#define FLIPPER_OS_SETTINGS_MAGIC 0x05
#define FLIPPER_OS_SETTINGS_VERSION 1

void flipper_os_settings_defaults(FlipperOsSettings* settings) {
    memset(settings, 0, sizeof(FlipperOsSettings));
    settings->counter_step = 1;
    settings->dice_sides_index = 1; // d6
    settings->dice_count = 1;
    settings->flashlight_level = 3; // 100%
    settings->timer_seconds = 5 * 60;
}

void flipper_os_settings_load(FlipperOsSettings* settings) {
    if(!saved_struct_load(
           FLIPPER_OS_SETTINGS_PATH,
           settings,
           sizeof(FlipperOsSettings),
           FLIPPER_OS_SETTINGS_MAGIC,
           FLIPPER_OS_SETTINGS_VERSION)) {
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
