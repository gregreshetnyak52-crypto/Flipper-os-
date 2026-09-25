#include "countdown.h"

#include <furi.h>
#include <gui/elements.h>
#include <notification/notification_messages.h>

#define COUNTDOWN_REFRESH_MS 200
#define COUNTDOWN_MAX_SECONDS (99 * 60 + 59)

typedef enum {
    CountdownStateSetup,
    CountdownStateRunning,
    CountdownStatePaused,
    CountdownStateAlarm,
} CountdownState;

typedef enum {
    CountdownFieldMinutes,
    CountdownFieldSeconds,
} CountdownField;

struct Countdown {
    View* view;
    FuriTimer* timer;
    NotificationApp* notifications;
    FlipperOsSettings* settings;
};

typedef struct {
    CountdownState state;
    CountdownField field;
    uint16_t preset_s; // value the user dialled in
    uint32_t remaining_ms; // valid while paused
    uint32_t deadline_tick; // valid while running
    uint8_t alarm_ticks;
} CountdownModel;

static uint32_t countdown_remaining_ms(const CountdownModel* model) {
    if(model->state == CountdownStateRunning) {
        int32_t left = (int32_t)(model->deadline_tick - furi_get_tick());
        if(left <= 0) return 0;
        uint32_t freq = furi_kernel_get_tick_frequency();
        return (uint32_t)left / freq * 1000 + ((uint32_t)left % freq) * 1000 / freq;
    }
    if(model->state == CountdownStatePaused) return model->remaining_ms;
    if(model->state == CountdownStateAlarm) return 0;
    return model->preset_s * 1000;
}

static void countdown_start_from(CountdownModel* model, uint32_t ms) {
    model->deadline_tick = furi_get_tick() + furi_ms_to_ticks(ms);
    model->state = CountdownStateRunning;
}

static void countdown_draw_callback(Canvas* canvas, void* _model) {
    CountdownModel* model = _model;
    char buf[16];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Countdown");

    // Round up so the display reaches 00:00 exactly when the alarm fires
    uint32_t ms = countdown_remaining_ms(model);
    uint32_t total_s = (ms + 999) / 1000;
    snprintf(
        buf,
        sizeof(buf),
        "%02lu:%02lu",
        (unsigned long)(total_s / 60),
        (unsigned long)(total_s % 60));
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignCenter, buf);

    canvas_set_font(canvas, FontSecondary);
    if(model->state == CountdownStateSetup) {
        // Underline the field being edited
        uint8_t x = model->field == CountdownFieldMinutes ? 38 : 70;
        canvas_draw_box(canvas, x, 38, 22, 2);
        canvas_draw_str_aligned(canvas, 64, 46, AlignCenter, AlignCenter, "Arrows: set time");
        elements_button_center(canvas, "Start");
    } else if(model->state == CountdownStateAlarm) {
        if(model->alarm_ticks % 2) {
            canvas_draw_str_aligned(canvas, 64, 46, AlignCenter, AlignCenter, "TIME IS UP!");
        }
        elements_button_center(canvas, "Stop");
    } else {
        if(model->preset_s > 0) {
            elements_progress_bar(
                canvas, 4, 40, 120, 1.0f - (float)ms / (float)(model->preset_s * 1000));
        }
        elements_button_center(canvas, model->state == CountdownStateRunning ? "Pause" : "Resume");
        elements_button_left(canvas, "Reset");
    }
}

static bool countdown_input_callback(InputEvent* event, void* context) {
    Countdown* instance = context;
    if(event->key == InputKeyBack) return false;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return true;

    bool silence = false;
    with_view_model(
        instance->view,
        CountdownModel * model,
        {
            bool is_short = event->type == InputTypeShort;
            switch(model->state) {
            case CountdownStateSetup:
                if(event->key == InputKeyLeft || event->key == InputKeyRight) {
                    if(is_short) model->field = !model->field;
                } else if(event->key == InputKeyUp || event->key == InputKeyDown) {
                    int32_t step = model->field == CountdownFieldMinutes ? 60 : 1;
                    if(event->type == InputTypeRepeat && model->field == CountdownFieldSeconds) {
                        step = 5;
                    }
                    int32_t value = model->preset_s;
                    value += event->key == InputKeyUp ? step : -step;
                    model->preset_s = CLAMP(value, COUNTDOWN_MAX_SECONDS, 0);
                } else if(event->key == InputKeyOk && is_short && model->preset_s > 0) {
                    countdown_start_from(model, model->preset_s * 1000);
                }
                break;
            case CountdownStateRunning:
                if(event->key == InputKeyOk && is_short) {
                    model->remaining_ms = countdown_remaining_ms(model);
                    model->state = CountdownStatePaused;
                } else if(event->key == InputKeyLeft && is_short) {
                    model->state = CountdownStateSetup;
                }
                break;
            case CountdownStatePaused:
                if(event->key == InputKeyOk && is_short) {
                    countdown_start_from(model, model->remaining_ms);
                } else if(event->key == InputKeyLeft && is_short) {
                    model->state = CountdownStateSetup;
                }
                break;
            case CountdownStateAlarm:
                // Any key silences the alarm
                model->state = CountdownStateSetup;
                silence = true;
                break;
            }
        },
        true);

    if(silence) notification_message(instance->notifications, &sequence_reset_rgb);
    return true;
}

static void countdown_tick_callback(void* context) {
    Countdown* instance = context;
    bool alarm = false;
    with_view_model(
        instance->view,
        CountdownModel * model,
        {
            if(model->state == CountdownStateRunning && countdown_remaining_ms(model) == 0) {
                model->state = CountdownStateAlarm;
                model->alarm_ticks = 0;
            }
            if(model->state == CountdownStateAlarm) {
                // Ring roughly once a second (every 5th refresh)
                alarm = model->alarm_ticks % 5 == 0;
                model->alarm_ticks++;
            }
        },
        true);

    if(alarm) notification_message(instance->notifications, &sequence_audiovisual_alert);
}

static void countdown_enter_callback(void* context) {
    Countdown* instance = context;
    furi_timer_start(instance->timer, furi_ms_to_ticks(COUNTDOWN_REFRESH_MS));
}

static void countdown_exit_callback(void* context) {
    Countdown* instance = context;
    // A running countdown keeps its deadline and catches up when reopened
    furi_timer_stop(instance->timer);
    with_view_model(
        instance->view,
        CountdownModel * model,
        {
            if(model->state == CountdownStateAlarm) model->state = CountdownStateSetup;
            instance->settings->timer_seconds = model->preset_s;
        },
        false);
    notification_message(instance->notifications, &sequence_reset_rgb);
}

Countdown* countdown_alloc(FlipperOsSettings* settings) {
    Countdown* instance = malloc(sizeof(Countdown));
    instance->settings = settings;
    instance->notifications = furi_record_open(RECORD_NOTIFICATION);
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(CountdownModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, countdown_draw_callback);
    view_set_input_callback(instance->view, countdown_input_callback);
    view_set_enter_callback(instance->view, countdown_enter_callback);
    view_set_exit_callback(instance->view, countdown_exit_callback);
    instance->timer = furi_timer_alloc(countdown_tick_callback, FuriTimerTypePeriodic, instance);

    with_view_model(
        instance->view,
        CountdownModel * model,
        {
            model->state = CountdownStateSetup;
            model->field = CountdownFieldMinutes;
            model->preset_s = MIN(settings->timer_seconds, COUNTDOWN_MAX_SECONDS);
        },
        false);
    return instance;
}

void countdown_free(Countdown* instance) {
    furi_assert(instance);
    furi_timer_free(instance->timer);
    view_free(instance->view);
    furi_record_close(RECORD_NOTIFICATION);
    free(instance);
}

View* countdown_get_view(Countdown* instance) {
    return instance->view;
}
