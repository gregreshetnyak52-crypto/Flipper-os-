#include "counter.h"

#include <furi.h>
#include <gui/elements.h>
#include <notification/notification_messages.h>

#define COUNTER_MAX 9999

struct Counter {
    View* view;
    NotificationApp* notifications;
    FlipperOsSettings* settings;
};

typedef struct {
    int16_t value;
    int16_t step;
} CounterModel;

static void counter_draw_callback(Canvas* canvas, void* _model) {
    CounterModel* model = _model;
    char buf[16];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Tally Counter");
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "Step: %d", model->step);
    canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignBottom, buf);

    snprintf(buf, sizeof(buf), "%d", model->value);
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignCenter, buf);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 45, AlignCenter, AlignCenter, "Hold Left: reset");
    elements_button_center(canvas, "+");
    elements_button_left(canvas, "-");
}

static bool counter_input_callback(InputEvent* event, void* context) {
    Counter* instance = context;
    bool consumed = true;
    bool feedback = false;

    with_view_model(
        instance->view,
        CounterModel * model,
        {
            bool press = event->type == InputTypeShort || event->type == InputTypeRepeat;
            if(event->key == InputKeyOk && press) {
                model->value += model->step;
                if(model->value > COUNTER_MAX) model->value = COUNTER_MAX;
                feedback = true;
            } else if(event->key == InputKeyLeft && event->type == InputTypeLong) {
                model->value = 0;
                feedback = true;
            } else if(event->key == InputKeyLeft && press) {
                model->value -= model->step;
                if(model->value < -COUNTER_MAX) model->value = -COUNTER_MAX;
            } else if(event->key == InputKeyUp && press) {
                if(model->step < 100) model->step = model->step == 1 ? 5 : model->step * 2;
                if(model->step > 100) model->step = 100;
            } else if(event->key == InputKeyDown && press) {
                if(model->step == 5) {
                    model->step = 1;
                } else if(model->step > 5) {
                    model->step /= 2;
                    if(model->step < 5) model->step = 5;
                }
            } else {
                consumed = false;
            }
        },
        consumed);

    if(feedback) notification_message(instance->notifications, &sequence_single_vibro);
    return consumed;
}

static void counter_enter_callback(void* context) {
    Counter* instance = context;
    with_view_model(
        instance->view,
        CounterModel * model,
        {
            model->value = CLAMP(instance->settings->counter_value, COUNTER_MAX, -COUNTER_MAX);
            model->step = CLAMP(instance->settings->counter_step, 100, 1);
        },
        true);
}

static void counter_exit_callback(void* context) {
    Counter* instance = context;
    with_view_model(
        instance->view,
        CounterModel * model,
        {
            instance->settings->counter_value = model->value;
            instance->settings->counter_step = model->step;
        },
        false);
}

Counter* counter_alloc(FlipperOsSettings* settings) {
    Counter* instance = malloc(sizeof(Counter));
    instance->settings = settings;
    instance->notifications = furi_record_open(RECORD_NOTIFICATION);
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(CounterModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, counter_draw_callback);
    view_set_input_callback(instance->view, counter_input_callback);
    view_set_enter_callback(instance->view, counter_enter_callback);
    view_set_exit_callback(instance->view, counter_exit_callback);
    return instance;
}

void counter_free(Counter* instance) {
    furi_assert(instance);
    view_free(instance->view);
    furi_record_close(RECORD_NOTIFICATION);
    free(instance);
}

View* counter_get_view(Counter* instance) {
    return instance->view;
}
