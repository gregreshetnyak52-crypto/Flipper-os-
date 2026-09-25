#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>

#include "modules/stopwatch.h"
#include "modules/dice.h"
#include "modules/morse.h"
#include "modules/snake.h"
#include "modules/counter.h"
#include "modules/sysinfo.h"

#define FLIPPER_OS_VERSION "1.0"

typedef enum {
    FlipperOsViewMenu,
    FlipperOsViewStopwatch,
    FlipperOsViewDice,
    FlipperOsViewMorse,
    FlipperOsViewSnake,
    FlipperOsViewCounter,
    FlipperOsViewSysInfo,
} FlipperOsView;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* menu;

    Stopwatch* stopwatch;
    Dice* dice;
    Morse* morse;
    Snake* snake;
    Counter* counter;
    SysInfo* sysinfo;
} FlipperOsApp;

/** Previous-view callback shared by every module: Back returns to the main menu. */
uint32_t flipper_os_back_to_menu(void* context);
