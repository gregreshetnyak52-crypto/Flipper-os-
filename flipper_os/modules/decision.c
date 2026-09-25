#include "decision.h"

#include <furi.h>
#include <furi_hal_random.h>
#include <gui/elements.h>
#include <notification/notification_messages.h>

#define DECISION_ANIMATION_FRAMES 10
#define DECISION_ANIMATION_MS 70

typedef enum {
    DecisionModeCoin,
    DecisionModeYesNo,
    DecisionModeEightBall,
    DecisionModeCount,
} DecisionMode;

static const char* const decision_mode_names[DecisionModeCount] = {
    "Coin flip",
    "Yes / No",
    "Magic 8-Ball",
};

static const char* const decision_coin[] = {"HEADS", "TAILS"};
static const char* const decision_yes_no[] = {"YES", "NO"};

// The classic 20 answers: 10 positive, 5 non-committal, 5 negative
static const char* const decision_eight_ball[] = {
    "It is certain",
    "It is decidedly so",
    "Without a doubt",
    "Yes, definitely",
    "You may rely on it",
    "As I see it, yes",
    "Most likely",
    "Outlook good",
    "Yes",
    "Signs point to yes",
    "Reply hazy, try again",
    "Ask again later",
    "Better not tell now",
    "Cannot predict now",
    "Concentrate, ask again",
    "Don't count on it",
    "My reply is no",
    "My sources say no",
    "Outlook not so good",
    "Very doubtful",
};

static const struct {
    const char* const* answers;
    uint8_t count;
} decision_answers[DecisionModeCount] = {
    {decision_coin, COUNT_OF(decision_coin)},
    {decision_yes_no, COUNT_OF(decision_yes_no)},
    {decision_eight_ball, COUNT_OF(decision_eight_ball)},
};

struct Decision {
    View* view;
    FuriTimer* timer;
    NotificationApp* notifications;
    FlipperOsSettings* settings;
};

typedef struct {
    DecisionMode mode;
    uint8_t answer;
    bool answered;
    uint8_t frames_left;
    // Tally of results for two-way modes since the mode was picked
    uint16_t tally[2];
} DecisionModel;

static void decision_pick(DecisionModel* model) {
    model->answer = furi_hal_random_get() % decision_answers[model->mode].count;
    model->answered = true;
}

static void decision_draw_callback(Canvas* canvas, void* _model) {
    DecisionModel* model = _model;
    char buf[32];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Decide");
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "< %s >", decision_mode_names[model->mode]);
    canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignBottom, buf);

    if(!model->answered) {
        canvas_draw_str_aligned(
            canvas,
            64,
            28,
            AlignCenter,
            AlignCenter,
            model->mode == DecisionModeEightBall ? "Ask a question, press OK" : "Press OK");
    } else {
        const char* answer = decision_answers[model->mode].answers[model->answer];
        bool spinning = model->frames_left > 0;
        if(model->mode == DecisionModeEightBall) {
            // The answer shows up in the ball's window
            elements_frame(canvas, 2, 16, 124, 22);
            canvas_draw_str_aligned(
                canvas, 64, 27, AlignCenter, AlignCenter, spinning ? "..." : answer);
        } else {
            canvas_draw_circle(canvas, 64, 28, 13);
            if(!spinning) canvas_draw_circle(canvas, 64, 28, 11);
            canvas_set_font(canvas, FontPrimary);
            // Short labels fit inside the coin, show the first letter there
            snprintf(buf, sizeof(buf), "%c", answer[0]);
            canvas_draw_str_aligned(canvas, 64, 29, AlignCenter, AlignCenter, buf);
            if(!spinning) {
                canvas_draw_str_aligned(canvas, 18, 29, AlignCenter, AlignCenter, answer);
                canvas_set_font(canvas, FontSecondary);
                snprintf(
                    buf,
                    sizeof(buf),
                    "%c:%u",
                    decision_answers[model->mode].answers[0][0],
                    model->tally[0]);
                canvas_draw_str_aligned(canvas, 110, 24, AlignCenter, AlignCenter, buf);
                snprintf(
                    buf,
                    sizeof(buf),
                    "%c:%u",
                    decision_answers[model->mode].answers[1][0],
                    model->tally[1]);
                canvas_draw_str_aligned(canvas, 110, 34, AlignCenter, AlignCenter, buf);
            }
        }
    }

    elements_button_center(canvas, model->mode == DecisionModeCoin ? "Flip" : "Ask");
}

static bool decision_input_callback(InputEvent* event, void* context) {
    Decision* instance = context;
    if(event->type != InputTypeShort) return false;

    bool consumed = true;
    bool start = false;
    with_view_model(
        instance->view,
        DecisionModel * model,
        {
            if(model->frames_left > 0) {
                // Busy spinning
                consumed = event->key != InputKeyBack;
            } else if(event->key == InputKeyRight || event->key == InputKeyLeft) {
                int8_t step = event->key == InputKeyRight ? 1 : DecisionModeCount - 1;
                model->mode = (model->mode + step) % DecisionModeCount;
                model->answered = false;
                model->tally[0] = model->tally[1] = 0;
            } else if(event->key == InputKeyOk) {
                model->frames_left = DECISION_ANIMATION_FRAMES;
                decision_pick(model);
                start = true;
            } else {
                consumed = false;
            }
        },
        consumed);

    if(start) furi_timer_start(instance->timer, furi_ms_to_ticks(DECISION_ANIMATION_MS));
    return consumed;
}

static void decision_timer_callback(void* context) {
    Decision* instance = context;
    bool finished = false;
    with_view_model(
        instance->view,
        DecisionModel * model,
        {
            if(model->frames_left > 0) {
                model->frames_left--;
                decision_pick(model);
                if(model->frames_left == 0 && model->mode != DecisionModeEightBall) {
                    model->tally[model->answer]++;
                }
            }
            finished = model->frames_left == 0;
        },
        true);

    if(finished) {
        furi_timer_stop(instance->timer);
        notification_message(instance->notifications, &sequence_single_vibro);
    }
}

static void decision_enter_callback(void* context) {
    Decision* instance = context;
    with_view_model(
        instance->view,
        DecisionModel * model,
        {
            model->mode = instance->settings->decision_mode < DecisionModeCount ?
                              instance->settings->decision_mode :
                              DecisionModeCoin;
        },
        true);
}

static void decision_exit_callback(void* context) {
    Decision* instance = context;
    furi_timer_stop(instance->timer);
    with_view_model(
        instance->view,
        DecisionModel * model,
        {
            if(model->frames_left > 0) {
                // Settle on the answer that was already drawn
                model->frames_left = 0;
                if(model->mode != DecisionModeEightBall) model->tally[model->answer]++;
            }
            instance->settings->decision_mode = model->mode;
        },
        false);
}

Decision* decision_alloc(FlipperOsSettings* settings) {
    Decision* instance = malloc(sizeof(Decision));
    instance->settings = settings;
    instance->notifications = furi_record_open(RECORD_NOTIFICATION);
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(DecisionModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, decision_draw_callback);
    view_set_input_callback(instance->view, decision_input_callback);
    view_set_enter_callback(instance->view, decision_enter_callback);
    view_set_exit_callback(instance->view, decision_exit_callback);
    instance->timer = furi_timer_alloc(decision_timer_callback, FuriTimerTypePeriodic, instance);
    return instance;
}

void decision_free(Decision* instance) {
    furi_assert(instance);
    furi_timer_free(instance->timer);
    view_free(instance->view);
    furi_record_close(RECORD_NOTIFICATION);
    free(instance);
}

View* decision_get_view(Decision* instance) {
    return instance->view;
}
