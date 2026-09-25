#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FLIPPER_OS_VERSION "1.2"

/**
 * Persistent state shared by all modules.
 *
 * Modules read their initial values on alloc and write them back when their
 * view is closed. The app loads the file on start and saves it on exit.
 * Bump FLIPPER_OS_SETTINGS_VERSION whenever the layout changes.
 */
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
} FlipperOsSettings;

void flipper_os_settings_load(FlipperOsSettings* settings);
void flipper_os_settings_save(const FlipperOsSettings* settings);

/** Reset to defaults in memory (does not touch the file on disk). */
void flipper_os_settings_defaults(FlipperOsSettings* settings);
