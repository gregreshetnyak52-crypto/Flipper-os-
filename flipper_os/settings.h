#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FLIPPER_OS_VERSION "1.3"

#define FLIPPER_OS_MORSE_TEXT_LEN 24

/**
 * Persistent state shared by all modules.
 *
 * Modules read their values when their view is opened and write them back
 * when it is closed. The app loads the file on start and saves it on exit.
 *
 * Bump FLIPPER_OS_SETTINGS_VERSION whenever the layout changes, only ever
 * append fields at the end and teach flipper_os_settings_load() to migrate
 * the previous layout, so users keep their records across updates.
 */
typedef struct {
    // Version 1
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
    // Version 2
    uint16_t snake_best_walls;
    bool snake_walls;
    char morse_text[FLIPPER_OS_MORSE_TEXT_LEN + 1];
    uint8_t pomodoro_minutes[3]; // focus, short break, long break
    uint16_t pomodoro_total;
    uint8_t password_length;
    uint8_t password_charset;
    uint16_t reaction_best_ms;
    uint8_t decision_mode;
} FlipperOsSettings;

void flipper_os_settings_load(FlipperOsSettings* settings);
void flipper_os_settings_save(const FlipperOsSettings* settings);

/** Reset to defaults in memory (does not touch the file on disk). */
void flipper_os_settings_defaults(FlipperOsSettings* settings);

/**
 * Requests a module makes to the app. Modules raise them from their own
 * timers or input handlers; the app handles them on its own thread.
 */
typedef enum {
    FlipperOsEventShowCountdown, // the countdown went off while not on screen
    FlipperOsEventShowPomodoro, // a pomodoro phase ended while not on screen
    FlipperOsEventEditMorseText, // open the keyboard for the custom Morse text
    FlipperOsEventMorseTextDone, // keyboard closed, go back to Morse
} FlipperOsEvent;

typedef void (*FlipperOsEventCallback)(void* context, FlipperOsEvent event);
