#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>

#include "settings.h"
#include "modules/stopwatch.h"
#include "modules/countdown.h"
#include "modules/flashlight.h"
#include "modules/dice.h"
#include "modules/morse.h"
#include "modules/snake.h"
#include "modules/counter.h"
#include "modules/sysinfo.h"

#define FLIPPER_OS_VERSION "1.1"

typedef enum {
    FlipperOsViewMenu,
    FlipperOsViewStopwatch,
    FlipperOsViewCountdown,
    FlipperOsViewFlashlight,
    FlipperOsViewDice,
    FlipperOsViewMorse,
    FlipperOsViewSnake,
    FlipperOsViewCounter,
    FlipperOsViewSysInfo,
    FlipperOsViewCount,
} FlipperOsView;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* menu;
    FlipperOsSettings settings;

    Stopwatch* stopwatch;
    Countdown* countdown;
    Flashlight* flashlight;
    Dice* dice;
    Morse* morse;
    Snake* snake;
    Counter* counter;
    SysInfo* sysinfo;
} FlipperOsApp;

/** Previous-view callback shared by every module: Back returns to the main menu. */
uint32_t flipper_os_back_to_menu(void* context);
