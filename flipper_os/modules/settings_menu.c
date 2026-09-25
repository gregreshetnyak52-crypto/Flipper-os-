#include "settings_menu.h"

#include <furi.h>
#include <gui/elements.h>
#include <notification/notification_messages.h>

#include "../settings.h"

#define SETTINGS_ITEMS_COUNT 3
#define SETTINGS_STATUS_MS 1500

static const char* const settings_item_names[SETTINGS_ITEMS_COUNT] = {
    "Reset records and stats",
    "Reset tally counter",
    "Reset all Toolkit data",
};

struct SettingsMenu {
    View* view;
    FuriTimer* status_timer;
    NotificationApp* notifications;
    FlipperOsSettings* settings;
};

typedef struct {
    uint8_t cursor;
    bool confirming;
    const char* status;
} SettingsMenuModel;

static void settings_menu_apply(SettingsMenu* instance, uint8_t index) {
    FlipperOsSettings* settings = instance->settings;
    switch(index) {
    case 0:
        settings->snake_best = 0;
        settings->snake_best_walls = 0;
        settings->reaction_best_ms = 0;
        settings->pomodoro_total = 0;
        break;
    case 1:
        settings->counter_value = 0;
        settings->counter_step = 1;
        break;
    case 2:
        flipper_os_settings_defaults(settings);
        break;
    }
    // Modules re-read the settings whenever they are opened, so the reset
    // is visible right away; persist it too in case the app is killed.
    flipper_os_settings_save(settings);
}

static void settings_menu_draw_callback(Canvas* canvas, void* _model) {
    SettingsMenuModel* model = _model;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Settings");

    canvas_set_font(canvas, FontSecondary);
    for(uint8_t i = 0; i < SETTINGS_ITEMS_COUNT; i++) {
        uint8_t y = 24 + i * 12;
        if(i == model->cursor) {
            canvas_draw_box(canvas, 0, y - 9, 128, 11);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, 4, y, settings_item_names[i]);
        canvas_set_color(canvas, ColorBlack);
    }

    if(model->confirming) {
        const char* message = "Sure? OK - yes, Back - no";
        canvas_set_font(canvas, FontSecondary);
        uint16_t w = canvas_string_width(canvas, message) + 6;
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 64 - w / 2, 56, w, 10);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, 64 - w / 2, 56, w, 10);
        canvas_draw_str_aligned(canvas, 64, 61, AlignCenter, AlignCenter, message);
    } else if(model->status) {
        canvas_draw_str_aligned(canvas, 64, 61, AlignCenter, AlignCenter, model->status);
    }
}

static bool settings_menu_input_callback(InputEvent* event, void* context) {
    SettingsMenu* instance = context;
    if(event->type != InputTypeShort) return false;

    bool consumed = true;
    bool applied = false;
    with_view_model(
        instance->view,
        SettingsMenuModel * model,
        {
            if(model->confirming) {
                if(event->key == InputKeyOk) {
                    settings_menu_apply(instance, model->cursor);
                    model->status = "Done.";
                    applied = true;
                }
                model->confirming = false;
            } else if(event->key == InputKeyUp) {
                model->cursor = (model->cursor + SETTINGS_ITEMS_COUNT - 1) % SETTINGS_ITEMS_COUNT;
                model->status = NULL;
            } else if(event->key == InputKeyDown) {
                model->cursor = (model->cursor + 1) % SETTINGS_ITEMS_COUNT;
                model->status = NULL;
            } else if(event->key == InputKeyOk) {
                model->confirming = true;
                model->status = NULL;
            } else {
                consumed = false;
            }
        },
        true);

    if(applied) {
        notification_message(instance->notifications, &sequence_single_vibro);
        furi_timer_start(instance->status_timer, furi_ms_to_ticks(SETTINGS_STATUS_MS));
    }
    return consumed;
}

static void settings_menu_status_timer_callback(void* context) {
    SettingsMenu* instance = context;
    with_view_model(instance->view, SettingsMenuModel * model, { model->status = NULL; }, true);
}

SettingsMenu* settings_menu_alloc(FlipperOsSettings* settings) {
    SettingsMenu* instance = malloc(sizeof(SettingsMenu));
    instance->settings = settings;
    instance->notifications = furi_record_open(RECORD_NOTIFICATION);
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(SettingsMenuModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, settings_menu_draw_callback);
    view_set_input_callback(instance->view, settings_menu_input_callback);
    instance->status_timer =
        furi_timer_alloc(settings_menu_status_timer_callback, FuriTimerTypeOnce, instance);

    with_view_model(
        instance->view,
        SettingsMenuModel * model,
        {
            model->cursor = 0;
            model->confirming = false;
            model->status = NULL;
        },
        false);
    return instance;
}

void settings_menu_free(SettingsMenu* instance) {
    furi_assert(instance);
    furi_timer_free(instance->status_timer);
    view_free(instance->view);
    furi_record_close(RECORD_NOTIFICATION);
    free(instance);
}

View* settings_menu_get_view(SettingsMenu* instance) {
    return instance->view;
}
