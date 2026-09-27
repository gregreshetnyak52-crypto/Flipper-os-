#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>

#include "settings.h"
#include "modules/stopwatch.h"
#include "modules/countdown.h"
#include "modules/pomodoro.h"
#include "modules/flashlight.h"
#include "modules/dice.h"
#include "modules/decision.h"
#include "modules/password.h"
#include "modules/morse.h"
#include "modules/snake.h"
#include "modules/reaction.h"
#include "modules/counter.h"
#include "modules/sysinfo.h"
#include "modules/settings_menu.h"
#include "modules/about.h"

typedef enum {
    FlipperOsViewMenu,
    FlipperOsViewStopwatch,
    FlipperOsViewCountdown,
    FlipperOsViewPomodoro,
    FlipperOsViewFlashlight,
    FlipperOsViewDice,
    FlipperOsViewDecision,
    FlipperOsViewPassword,
    FlipperOsViewMorse,
    FlipperOsViewSnake,
    FlipperOsViewReaction,
    FlipperOsViewCounter,
    FlipperOsViewSysInfo,
    FlipperOsViewSettings,
    FlipperOsViewAbout,
    FlipperOsViewTextInput, // keyboard for the custom Morse text, not in the menu
    FlipperOsViewCount,
} FlipperOsView;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* menu;
    TextInput* text_input;
    char text_buffer[FLIPPER_OS_MORSE_TEXT_LEN + 1];
    FlipperOsSettings settings;

    Stopwatch* stopwatch;
    Countdown* countdown;
    Pomodoro* pomodoro;
    Flashlight* flashlight;
    Dice* dice;
    Decision* decision;
    Password* password;
    Morse* morse;
    Snake* snake;
    Reaction* reaction;
    Counter* counter;
    SysInfo* sysinfo;
    SettingsMenu* settings_menu;
    About* about;
} FlipperOsApp;

/** Previous-view callback shared by every module: Back returns to the main menu. */
uint32_t flipper_os_back_to_menu(void* context);
