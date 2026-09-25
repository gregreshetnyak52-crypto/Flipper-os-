#include "flipper_os_i.h"

uint32_t flipper_os_back_to_menu(void* context) {
    UNUSED(context);
    return FlipperOsViewMenu;
}

// VIEW_IGNORE hands Back over to the navigation callback, which exits the app
static uint32_t flipper_os_exit(void* context) {
    UNUSED(context);
    return VIEW_IGNORE;
}

static void flipper_os_menu_callback(void* context, uint32_t index) {
    FlipperOsApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, index);
}

static bool flipper_os_navigation_callback(void* context) {
    FlipperOsApp* app = context;
    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

static void flipper_os_add_module(FlipperOsApp* app, FlipperOsView id, const char* name, View* view) {
    view_set_previous_callback(view, flipper_os_back_to_menu);
    view_dispatcher_add_view(app->view_dispatcher, id, view);
    submenu_add_item(app->menu, name, id, flipper_os_menu_callback, app);
}

static FlipperOsApp* flipper_os_alloc(void) {
    FlipperOsApp* app = malloc(sizeof(FlipperOsApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, flipper_os_navigation_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->menu = submenu_alloc();
    submenu_set_header(app->menu, "Flipper OS v" FLIPPER_OS_VERSION);
    View* menu_view = submenu_get_view(app->menu);
    view_set_previous_callback(menu_view, flipper_os_exit);
    view_dispatcher_add_view(app->view_dispatcher, FlipperOsViewMenu, menu_view);

    flipper_os_settings_load(&app->settings);
    FlipperOsSettings* settings = &app->settings;

    app->stopwatch = stopwatch_alloc(settings);
    app->countdown = countdown_alloc(settings);
    app->flashlight = flashlight_alloc(settings);
    app->dice = dice_alloc(settings);
    app->morse = morse_alloc(settings);
    app->snake = snake_alloc(settings);
    app->counter = counter_alloc(settings);
    app->sysinfo = sysinfo_alloc(settings);

    flipper_os_add_module(
        app, FlipperOsViewStopwatch, "Stopwatch", stopwatch_get_view(app->stopwatch));
    flipper_os_add_module(
        app, FlipperOsViewCountdown, "Timer", countdown_get_view(app->countdown));
    flipper_os_add_module(
        app, FlipperOsViewFlashlight, "Flashlight", flashlight_get_view(app->flashlight));
    flipper_os_add_module(app, FlipperOsViewDice, "Dice Roller", dice_get_view(app->dice));
    flipper_os_add_module(app, FlipperOsViewMorse, "Morse Beacon", morse_get_view(app->morse));
    flipper_os_add_module(app, FlipperOsViewSnake, "Snake", snake_get_view(app->snake));
    flipper_os_add_module(
        app, FlipperOsViewCounter, "Tally Counter", counter_get_view(app->counter));
    flipper_os_add_module(
        app, FlipperOsViewSysInfo, "System Info", sysinfo_get_view(app->sysinfo));

    return app;
}

static void flipper_os_free(FlipperOsApp* app) {
    // Removing the views fires the active module's exit callback, which
    // writes its state back into app->settings, so save only afterwards.
    for(uint32_t id = FlipperOsViewMenu; id < FlipperOsViewCount; id++) {
        view_dispatcher_remove_view(app->view_dispatcher, id);
    }
    flipper_os_settings_save(&app->settings);

    stopwatch_free(app->stopwatch);
    countdown_free(app->countdown);
    flashlight_free(app->flashlight);
    dice_free(app->dice);
    morse_free(app->morse);
    snake_free(app->snake);
    counter_free(app->counter);
    sysinfo_free(app->sysinfo);
    submenu_free(app->menu);

    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t flipper_os_app(void* p) {
    UNUSED(p);
    FlipperOsApp* app = flipper_os_alloc();
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperOsViewMenu);
    view_dispatcher_run(app->view_dispatcher);
    flipper_os_free(app);
    return 0;
}
