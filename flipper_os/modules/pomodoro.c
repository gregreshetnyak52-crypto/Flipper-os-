#include "pomodoro.h"

#include <furi.h>
#include <gui/elements.h>
#include <notification/notification_messages.h>

#define POMODORO_REFRESH_MS 200
#define POMODORO_RING_TICKS (10 * 1000 / POMODORO_REFRESH_MS)
#define POMODORO_FOCUS_PER_SET 4

typedef enum {
    PomodoroPhaseFocus,
    PomodoroPhaseBreak,
    PomodoroPhaseLongBreak,
    PomodoroPhaseCount,
} PomodoroPhase;

typedef enum {
    PomodoroStateSetup,
    PomodoroStateRunning,
    PomodoroStatePaused,
    PomodoroStateRing, // a phase ended, waiting for OK to start the next one
} PomodoroState;

static const char* const pomodoro_phase_names[PomodoroPhaseCount] = {
    "Focus",
    "Break",
    "Long break",
};
static const uint8_t pomodoro_minutes_default[PomodoroPhaseCount] = {25, 5, 15};
static const uint8_t pomodoro_minutes_max[PomodoroPhaseCount] = {90, 30, 60};

struct Pomodoro {
    View* view;
    FuriTimer* timer;
    NotificationApp* notifications;
    FlipperOsSettings* settings;
    FlipperOsEventCallback event_callback;
    void* event_context;
    bool active; // view is on screen
};

typedef struct {
    PomodoroState state;
    PomodoroPhase phase;
    uint8_t cursor; // setup row
    uint8_t minutes[PomodoroPhaseCount];
    uint8_t focus_done; // focus sessions finished in the current set
    uint16_t total; // focus sessions finished ever
    uint32_t remaining_ms; // valid while paused
    uint32_t deadline_tick; // valid while running
    uint16_t ring_ticks;
} PomodoroModel;

static uint32_t pomodoro_phase_ms(const PomodoroModel* model) {
    return model->minutes[model->phase] * 60 * 1000;
}

static uint32_t pomodoro_remaining_ms(const PomodoroModel* model) {
    switch(model->state) {
    case PomodoroStateRunning: {
        int32_t left = (int32_t)(model->deadline_tick - furi_get_tick());
        if(left <= 0) return 0;
        uint32_t freq = furi_kernel_get_tick_frequency();
        return (uint32_t)left / freq * 1000 + ((uint32_t)left % freq) * 1000 / freq;
    }
    case PomodoroStatePaused:
        return model->remaining_ms;
    default:
        return pomodoro_phase_ms(model);
    }
}

static void pomodoro_start(PomodoroModel* model, uint32_t ms) {
    model->deadline_tick = furi_get_tick() + furi_ms_to_ticks(ms);
    model->state = PomodoroStateRunning;
}

/**
 * Move on to the next phase. A focus session only counts towards the long
 * break (and the total) when it ran to the end, not when it was skipped.
 */
static void pomodoro_advance(Pomodoro* instance, PomodoroModel* model, bool completed) {
    if(model->phase == PomodoroPhaseFocus) {
        if(completed) {
            model->focus_done++;
            model->total++;
            instance->settings->pomodoro_total = model->total;
        }
        bool long_break = completed && model->focus_done >= POMODORO_FOCUS_PER_SET;
        model->phase = long_break ? PomodoroPhaseLongBreak : PomodoroPhaseBreak;
    } else {
        if(model->phase == PomodoroPhaseLongBreak) model->focus_done = 0;
        model->phase = PomodoroPhaseFocus;
    }
}

static void pomodoro_draw_header(Canvas* canvas, const PomodoroModel* model, const char* title) {
    char buf[24];
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, title);
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "Done: %u", model->total);
    canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignBottom, buf);
}

static void pomodoro_draw_setup(Canvas* canvas, const PomodoroModel* model) {
    char buf[24];
    pomodoro_draw_header(canvas, model, "Pomodoro");

    canvas_set_font(canvas, FontSecondary);
    for(uint8_t i = 0; i < PomodoroPhaseCount; i++) {
        uint8_t y = 22 + i * 10;
        bool selected = i == model->cursor;
        if(selected) {
            canvas_draw_box(canvas, 0, y - 8, 128, 10);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, 4, y, pomodoro_phase_names[i]);
        snprintf(
            buf, sizeof(buf), selected ? "< %u min >" : "%u min", model->minutes[i]);
        canvas_draw_str_aligned(canvas, 124, y, AlignRight, AlignBottom, buf);
        canvas_set_color(canvas, ColorBlack);
    }

    elements_button_center(canvas, "Start");
}

static void pomodoro_draw_callback(Canvas* canvas, void* _model) {
    PomodoroModel* model = _model;
    char buf[32];

    canvas_clear(canvas);
    if(model->state == PomodoroStateSetup) {
        pomodoro_draw_setup(canvas, model);
        return;
    }

    if(model->phase == PomodoroPhaseFocus) {
        snprintf(
            buf, sizeof(buf), "Focus %u/%u", model->focus_done + 1, POMODORO_FOCUS_PER_SET);
    } else {
        snprintf(buf, sizeof(buf), "%s", pomodoro_phase_names[model->phase]);
    }
    pomodoro_draw_header(canvas, model, buf);

    if(model->state == PomodoroStateRing) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(
            canvas,
            64,
            26,
            AlignCenter,
            AlignCenter,
            model->phase == PomodoroPhaseFocus ? "Break is over!" : "Time for a break!");
        canvas_set_font(canvas, FontSecondary);
        snprintf(
            buf,
            sizeof(buf),
            "Next: %s, %u min",
            pomodoro_phase_names[model->phase],
            model->minutes[model->phase]);
        canvas_draw_str_aligned(canvas, 64, 39, AlignCenter, AlignCenter, buf);
        elements_button_center(canvas, "Start");
        elements_button_left(canvas, "Reset");
        return;
    }

    uint32_t ms = pomodoro_remaining_ms(model);
    uint32_t total_s = (ms + 999) / 1000;
    snprintf(
        buf,
        sizeof(buf),
        "%02lu:%02lu",
        (unsigned long)(total_s / 60),
        (unsigned long)(total_s % 60));
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignCenter, buf);

    uint32_t phase_ms = pomodoro_phase_ms(model);
    if(phase_ms > 0) {
        elements_progress_bar(canvas, 4, 38, 120, 1.0f - (float)ms / (float)phase_ms);
    }

    elements_button_center(canvas, model->state == PomodoroStateRunning ? "Pause" : "Resume");
    elements_button_left(canvas, "Reset");
    elements_button_right(canvas, "Skip");
}

static bool pomodoro_input_callback(InputEvent* event, void* context) {
    Pomodoro* instance = context;
    if(event->key == InputKeyBack) return false;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return true;

    bool is_short = event->type == InputTypeShort;
    bool silence = false;
    with_view_model(
        instance->view,
        PomodoroModel * model,
        {
            switch(model->state) {
            case PomodoroStateSetup:
                if(event->key == InputKeyUp && is_short) {
                    model->cursor = (model->cursor + PomodoroPhaseCount - 1) % PomodoroPhaseCount;
                } else if(event->key == InputKeyDown && is_short) {
                    model->cursor = (model->cursor + 1) % PomodoroPhaseCount;
                } else if(event->key == InputKeyRight) {
                    uint8_t* value = &model->minutes[model->cursor];
                    if(*value < pomodoro_minutes_max[model->cursor]) (*value)++;
                } else if(event->key == InputKeyLeft) {
                    uint8_t* value = &model->minutes[model->cursor];
                    if(*value > 1) (*value)--;
                } else if(event->key == InputKeyOk && is_short) {
                    model->phase = PomodoroPhaseFocus;
                    model->focus_done = 0;
                    pomodoro_start(model, pomodoro_phase_ms(model));
                }
                break;
            case PomodoroStateRunning:
            case PomodoroStatePaused:
                if(!is_short) break;
                if(event->key == InputKeyOk) {
                    if(model->state == PomodoroStateRunning) {
                        model->remaining_ms = pomodoro_remaining_ms(model);
                        model->state = PomodoroStatePaused;
                    } else {
                        pomodoro_start(model, model->remaining_ms);
                    }
                } else if(event->key == InputKeyLeft) {
                    model->state = PomodoroStateSetup;
                } else if(event->key == InputKeyRight) {
                    pomodoro_advance(instance, model, false);
                    pomodoro_start(model, pomodoro_phase_ms(model));
                }
                break;
            case PomodoroStateRing:
                if(!is_short) break;
                if(event->key == InputKeyOk) {
                    pomodoro_start(model, pomodoro_phase_ms(model));
                    silence = true;
                } else if(event->key == InputKeyLeft) {
                    model->state = PomodoroStateSetup;
                    silence = true;
                } else {
                    // Any other key just stops the ringing
                    model->ring_ticks = POMODORO_RING_TICKS;
                    silence = true;
                }
                break;
            }
        },
        true);

    if(silence) notification_message(instance->notifications, &sequence_reset_rgb);
    return true;
}

static void pomodoro_tick_callback(void* context) {
    Pomodoro* instance = context;
    bool ring = false;
    bool fired = false;
    bool idle = false;
    with_view_model(
        instance->view,
        PomodoroModel * model,
        {
            if(model->state == PomodoroStateRunning && pomodoro_remaining_ms(model) == 0) {
                pomodoro_advance(instance, model, true);
                model->state = PomodoroStateRing;
                model->ring_ticks = 0;
                fired = true;
            }
            if(model->state == PomodoroStateRing && model->ring_ticks < POMODORO_RING_TICKS) {
                ring = model->ring_ticks % 5 == 0;
                model->ring_ticks++;
            }
            idle = model->state != PomodoroStateRunning &&
                   !(model->state == PomodoroStateRing && model->ring_ticks < POMODORO_RING_TICKS);
        },
        true);

    if(ring) notification_message(instance->notifications, &sequence_audiovisual_alert);
    if(idle && !instance->active) furi_timer_stop(instance->timer);
    if(fired && !instance->active && instance->event_callback) {
        instance->event_callback(instance->event_context, FlipperOsEventShowPomodoro);
    }
}

static void pomodoro_enter_callback(void* context) {
    Pomodoro* instance = context;
    FlipperOsSettings* settings = instance->settings;
    instance->active = true;
    with_view_model(
        instance->view,
        PomodoroModel * model,
        {
            // Settings may have been reset; durations only change between sessions
            model->total = settings->pomodoro_total;
            if(model->state == PomodoroStateSetup) {
                for(uint8_t i = 0; i < PomodoroPhaseCount; i++) {
                    uint8_t value = settings->pomodoro_minutes[i];
                    model->minutes[i] = value >= 1 && value <= pomodoro_minutes_max[i] ?
                                            value :
                                            pomodoro_minutes_default[i];
                }
            }
        },
        true);
    furi_timer_start(instance->timer, furi_ms_to_ticks(POMODORO_REFRESH_MS));
}

static void pomodoro_exit_callback(void* context) {
    Pomodoro* instance = context;
    instance->active = false;
    bool running = false;
    with_view_model(
        instance->view,
        PomodoroModel * model,
        {
            // Leaving the screen silences the bell; the next phase still waits for OK
            if(model->state == PomodoroStateRing) model->ring_ticks = POMODORO_RING_TICKS;
            running = model->state == PomodoroStateRunning;
            memcpy(
                instance->settings->pomodoro_minutes,
                model->minutes,
                sizeof(instance->settings->pomodoro_minutes));
        },
        false);
    // A running session keeps ticking so that it can ring from other screens
    if(!running) furi_timer_stop(instance->timer);
    notification_message(instance->notifications, &sequence_reset_rgb);
}

Pomodoro* pomodoro_alloc(FlipperOsSettings* settings) {
    Pomodoro* instance = malloc(sizeof(Pomodoro));
    instance->settings = settings;
    instance->event_callback = NULL;
    instance->event_context = NULL;
    instance->active = false;
    instance->notifications = furi_record_open(RECORD_NOTIFICATION);
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(PomodoroModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, pomodoro_draw_callback);
    view_set_input_callback(instance->view, pomodoro_input_callback);
    view_set_enter_callback(instance->view, pomodoro_enter_callback);
    view_set_exit_callback(instance->view, pomodoro_exit_callback);
    instance->timer = furi_timer_alloc(pomodoro_tick_callback, FuriTimerTypePeriodic, instance);

    with_view_model(
        instance->view,
        PomodoroModel * model,
        {
            model->state = PomodoroStateSetup;
            model->phase = PomodoroPhaseFocus;
        },
        false);
    return instance;
}

void pomodoro_free(Pomodoro* instance) {
    furi_assert(instance);
    furi_timer_stop(instance->timer);
    furi_timer_free(instance->timer);
    notification_message(instance->notifications, &sequence_reset_rgb);
    view_free(instance->view);
    furi_record_close(RECORD_NOTIFICATION);
    free(instance);
}

View* pomodoro_get_view(Pomodoro* instance) {
    return instance->view;
}

void pomodoro_set_event_callback(
    Pomodoro* instance,
    FlipperOsEventCallback callback,
    void* context) {
    instance->event_callback = callback;
    instance->event_context = context;
}
