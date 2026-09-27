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

// Back from the keyboard discards the edit and returns to Morse
static uint32_t flipper_os_back_to_morse(void* context) {
    UNUSED(context);
    return FlipperOsViewMorse;
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

// Modules raise events from their own threads: hand them to the GUI thread
static void flipper_os_module_event_callback(void* context, FlipperOsEvent event) {
    FlipperOsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

static void flipper_os_text_input_callback(void* context) {
    FlipperOsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, FlipperOsEventMorseTextDone);
}

static bool flipper_os_custom_event_callback(void* context, uint32_t event) {
    FlipperOsApp* app = context;
    switch(event) {
    case FlipperOsEventShowCountdown:
        view_dispatcher_switch_to_view(app->view_dispatcher, FlipperOsViewCountdown);
        return true;
    case FlipperOsEventShowPomodoro:
        view_dispatcher_switch_to_view(app->view_dispatcher, FlipperOsViewPomodoro);
        return true;
    case FlipperOsEventEditMorseText:
        // The keyboard has no space key, "_" stands in for it
        strlcpy(app->text_buffer, app->settings.morse_text, sizeof(app->text_buffer));
        for(char* c = app->text_buffer; *c; c++) {
            if(*c == ' ') *c = '_';
        }
        text_input_set_result_callback(
            app->text_input,
            flipper_os_text_input_callback,
            app,
            app->text_buffer,
            sizeof(app->text_buffer),
            false);
        view_dispatcher_switch_to_view(app->view_dispatcher, FlipperOsViewTextInput);
        return true;
    case FlipperOsEventMorseTextDone:
        for(char* c = app->text_buffer; *c; c++) {
            if(*c == '_') *c = ' ';
        }
        strlcpy(app->settings.morse_text, app->text_buffer, sizeof(app->settings.morse_text));
        view_dispatcher_switch_to_view(app->view_dispatcher, FlipperOsViewMorse);
        return true;
    default:
        return false;
    }
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
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, flipper_os_custom_event_callback);
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
    app->pomodoro = pomodoro_alloc(settings);
    app->flashlight = flashlight_alloc(settings);
    app->dice = dice_alloc(settings);
    app->decision = decision_alloc(settings);
    app->password = password_alloc(settings);
    app->morse = morse_alloc(settings);
    app->snake = snake_alloc(settings);
    app->reaction = reaction_alloc(settings);
    app->counter = counter_alloc(settings);
    app->sysinfo = sysinfo_alloc(settings);
    app->settings_menu = settings_menu_alloc(settings);
    app->about = about_alloc(settings);

    countdown_set_event_callback(app->countdown, flipper_os_module_event_callback, app);
    pomodoro_set_event_callback(app->pomodoro, flipper_os_module_event_callback, app);
    morse_set_event_callback(app->morse, flipper_os_module_event_callback, app);

    flipper_os_add_module(
        app, FlipperOsViewStopwatch, "Stopwatch", stopwatch_get_view(app->stopwatch));
    flipper_os_add_module(
        app, FlipperOsViewCountdown, "Timer", countdown_get_view(app->countdown));
    flipper_os_add_module(
        app, FlipperOsViewPomodoro, "Pomodoro", pomodoro_get_view(app->pomodoro));
    flipper_os_add_module(
        app, FlipperOsViewFlashlight, "Flashlight", flashlight_get_view(app->flashlight));
    flipper_os_add_module(app, FlipperOsViewDice, "Dice Roller", dice_get_view(app->dice));
    flipper_os_add_module(
        app, FlipperOsViewDecision, "Coin & 8-Ball", decision_get_view(app->decision));
    flipper_os_add_module(
        app, FlipperOsViewPassword, "Password Gen", password_get_view(app->password));
    flipper_os_add_module(app, FlipperOsViewMorse, "Morse Beacon", morse_get_view(app->morse));
    flipper_os_add_module(app, FlipperOsViewSnake, "Snake", snake_get_view(app->snake));
    flipper_os_add_module(
        app, FlipperOsViewReaction, "Reaction Test", reaction_get_view(app->reaction));
    flipper_os_add_module(
        app, FlipperOsViewCounter, "Tally Counter", counter_get_view(app->counter));
    flipper_os_add_module(
        app, FlipperOsViewSysInfo, "System Info", sysinfo_get_view(app->sysinfo));
    flipper_os_add_module(
        app, FlipperOsViewSettings, "Settings", settings_menu_get_view(app->settings_menu));
    flipper_os_add_module(app, FlipperOsViewAbout, "About", about_get_view(app->about));

    app->text_input = text_input_alloc();
    text_input_set_header_text(app->text_input, "Morse text, _ is a space");
    View* text_input_view = text_input_get_view(app->text_input);
    view_set_previous_callback(text_input_view, flipper_os_back_to_morse);
    view_dispatcher_add_view(app->view_dispatcher, FlipperOsViewTextInput, text_input_view);

    return app;
}

static void flipper_os_free(FlipperOsApp* app) {
    // Background timers must not post events to a dispatcher that is going away
    countdown_set_event_callback(app->countdown, NULL, NULL);
    pomodoro_set_event_callback(app->pomodoro, NULL, NULL);
    morse_set_event_callback(app->morse, NULL, NULL);

    // Removing the views fires the active module's exit callback, which
    // writes its state back into app->settings, so save only afterwards.
    for(uint32_t id = FlipperOsViewMenu; id < FlipperOsViewCount; id++) {
        view_dispatcher_remove_view(app->view_dispatcher, id);
    }
    flipper_os_settings_save(&app->settings);

    stopwatch_free(app->stopwatch);
    countdown_free(app->countdown);
    pomodoro_free(app->pomodoro);
    flashlight_free(app->flashlight);
    dice_free(app->dice);
    decision_free(app->decision);
    password_free(app->password);
    morse_free(app->morse);
    snake_free(app->snake);
    reaction_free(app->reaction);
    counter_free(app->counter);
    sysinfo_free(app->sysinfo);
    settings_menu_free(app->settings_menu);
    about_free(app->about);
    text_input_free(app->text_input);
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
