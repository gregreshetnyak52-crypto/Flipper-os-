#include "reaction.h"

#include <furi.h>
#include <furi_hal_random.h>
#include <gui/elements.h>
#include <notification/notification_messages.h>

#define REACTION_DELAY_MIN_MS 1500
#define REACTION_DELAY_SPREAD_MS 3000
#define REACTION_TIMEOUT_MS 3000
#define REACTION_HISTORY 5

typedef enum {
    ReactionStateIdle,
    ReactionStateWaiting, // random delay before the signal
    ReactionStateGo, // signal is on, measuring
    ReactionStateResult,
    ReactionStateTooEarly,
    ReactionStateTooSlow,
} ReactionState;

struct Reaction {
    View* view;
    FuriTimer* timer;
    NotificationApp* notifications;
    FlipperOsSettings* settings;
};

typedef struct {
    ReactionState state;
    uint32_t go_tick;
    uint16_t last_ms;
    uint16_t best_ms; // 0 when there is no record yet
    bool new_best;
    uint16_t history[REACTION_HISTORY];
    uint8_t history_count;
    // The press that ended a round must not start the next one on release
    bool swallow_release;
} ReactionModel;

static uint32_t reaction_ticks_to_ms(uint32_t ticks) {
    uint32_t freq = furi_kernel_get_tick_frequency();
    return ticks / freq * 1000 + (ticks % freq) * 1000 / freq;
}

static void reaction_draw_callback(Canvas* canvas, void* _model) {
    ReactionModel* model = _model;
    char buf[32];

    canvas_clear(canvas);

    if(model->state == ReactionStateGo) {
        canvas_draw_box(canvas, 0, 0, 128, 64);
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "PRESS!");
        return;
    }

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Reaction");
    canvas_set_font(canvas, FontSecondary);
    if(model->best_ms) {
        snprintf(buf, sizeof(buf), "Best: %u ms", model->best_ms);
    } else {
        snprintf(buf, sizeof(buf), "Best: -");
    }
    canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignBottom, buf);

    switch(model->state) {
    case ReactionStateIdle:
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, "Wait for the screen");
        canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignCenter, "and LED to flash, then");
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, "press any key fast");
        elements_button_center(canvas, "Start");
        break;
    case ReactionStateWaiting:
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignCenter, "Wait for it...");
        break;
    case ReactionStateResult: {
        snprintf(buf, sizeof(buf), "%u", model->last_ms);
        canvas_set_font(canvas, FontBigNumbers);
        canvas_draw_str_aligned(canvas, 60, 27, AlignRight, AlignCenter, buf);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 64, 32, "ms");
        if(model->new_best) canvas_draw_str(canvas, 86, 32, "Best!");

        uint32_t sum = 0;
        for(uint8_t i = 0; i < model->history_count; i++) sum += model->history[i];
        canvas_set_font(canvas, FontSecondary);
        snprintf(
            buf,
            sizeof(buf),
            "Average of %u: %lu ms",
            model->history_count,
            (unsigned long)(sum / model->history_count));
        canvas_draw_str_aligned(canvas, 64, 45, AlignCenter, AlignCenter, buf);
        elements_button_center(canvas, "Again");
        break;
    }
    case ReactionStateTooEarly:
    case ReactionStateTooSlow:
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(
            canvas,
            64,
            30,
            AlignCenter,
            AlignCenter,
            model->state == ReactionStateTooEarly ? "Too early!" : "Too slow!");
        elements_button_center(canvas, "Retry");
        break;
    case ReactionStateGo:
        break;
    }
}

static void reaction_signal_off(Reaction* instance) {
    notification_message(instance->notifications, &sequence_reset_rgb);
}

static bool reaction_input_callback(InputEvent* event, void* context) {
    Reaction* instance = context;
    if(event->key == InputKeyBack) return false;

    uint32_t now = furi_get_tick();
    bool arm = false;
    bool stop = false;
    bool feedback_ok = false;
    bool feedback_error = false;
    with_view_model(
        instance->view,
        ReactionModel * model,
        {
            if(event->type == InputTypePress) {
                if(model->state == ReactionStateGo) {
                    model->last_ms = reaction_ticks_to_ms(now - model->go_tick);
                    model->new_best = !model->best_ms || model->last_ms < model->best_ms;
                    if(model->new_best) model->best_ms = model->last_ms;
                    if(model->history_count == REACTION_HISTORY) {
                        memmove(
                            model->history,
                            model->history + 1,
                            sizeof(uint16_t) * (REACTION_HISTORY - 1));
                        model->history_count--;
                    }
                    model->history[model->history_count++] = model->last_ms;
                    model->state = ReactionStateResult;
                    model->swallow_release = true;
                    stop = true;
                    feedback_ok = true;
                } else if(model->state == ReactionStateWaiting) {
                    model->state = ReactionStateTooEarly;
                    model->swallow_release = true;
                    stop = true;
                    feedback_error = true;
                }
            } else if(event->type == InputTypeShort) {
                if(model->swallow_release) {
                    model->swallow_release = false;
                } else if(event->key == InputKeyOk && model->state != ReactionStateWaiting) {
                    model->state = ReactionStateWaiting;
                    arm = true;
                }
            } else if(event->type == InputTypeLong) {
                // No Short follows a long press, so nothing left to swallow
                model->swallow_release = false;
            }
        },
        true);

    if(stop) {
        furi_timer_stop(instance->timer);
        reaction_signal_off(instance);
    }
    if(feedback_ok) notification_message(instance->notifications, &sequence_single_vibro);
    if(feedback_error) notification_message(instance->notifications, &sequence_error);
    if(arm) {
        notification_message(instance->notifications, &sequence_display_backlight_on);
        uint32_t delay = REACTION_DELAY_MIN_MS + furi_hal_random_get() % REACTION_DELAY_SPREAD_MS;
        furi_timer_start(instance->timer, furi_ms_to_ticks(delay));
    }
    return true;
}

static void reaction_timer_callback(void* context) {
    Reaction* instance = context;
    bool go = false;
    bool timeout = false;
    with_view_model(
        instance->view,
        ReactionModel * model,
        {
            if(model->state == ReactionStateWaiting) {
                model->state = ReactionStateGo;
                model->go_tick = furi_get_tick();
                go = true;
            } else if(model->state == ReactionStateGo) {
                model->state = ReactionStateTooSlow;
                timeout = true;
            }
        },
        true);

    if(go) {
        notification_message(instance->notifications, &sequence_set_only_green_255);
        furi_timer_start(instance->timer, furi_ms_to_ticks(REACTION_TIMEOUT_MS));
    }
    if(timeout) reaction_signal_off(instance);
}

static void reaction_enter_callback(void* context) {
    Reaction* instance = context;
    with_view_model(
        instance->view,
        ReactionModel * model,
        {
            model->best_ms = instance->settings->reaction_best_ms;
            model->state = ReactionStateIdle;
            model->swallow_release = false;
        },
        true);
}

static void reaction_exit_callback(void* context) {
    Reaction* instance = context;
    furi_timer_stop(instance->timer);
    reaction_signal_off(instance);
    with_view_model(
        instance->view,
        ReactionModel * model,
        { instance->settings->reaction_best_ms = model->best_ms; },
        false);
}

Reaction* reaction_alloc(FlipperOsSettings* settings) {
    Reaction* instance = malloc(sizeof(Reaction));
    instance->settings = settings;
    instance->notifications = furi_record_open(RECORD_NOTIFICATION);
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(ReactionModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, reaction_draw_callback);
    view_set_input_callback(instance->view, reaction_input_callback);
    view_set_enter_callback(instance->view, reaction_enter_callback);
    view_set_exit_callback(instance->view, reaction_exit_callback);
    instance->timer = furi_timer_alloc(reaction_timer_callback, FuriTimerTypeOnce, instance);
    return instance;
}

void reaction_free(Reaction* instance) {
    furi_assert(instance);
    furi_timer_free(instance->timer);
    view_free(instance->view);
    furi_record_close(RECORD_NOTIFICATION);
    free(instance);
}

View* reaction_get_view(Reaction* instance) {
    return instance->view;
}
