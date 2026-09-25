#include "dice.h"

#include <furi.h>
#include <furi_hal_random.h>
#include <gui/elements.h>
#include <notification/notification_messages.h>

#define DICE_MAX_COUNT 4
#define DICE_ANIMATION_FRAMES 8
#define DICE_ANIMATION_MS 60

static const uint8_t dice_sides[] = {4, 6, 8, 10, 12, 20, 100};
#define DICE_SIDES_COUNT COUNT_OF(dice_sides)

struct Dice {
    View* view;
    FuriTimer* timer;
    NotificationApp* notifications;
    FlipperOsSettings* settings;
};

typedef struct {
    uint8_t sides_index;
    uint8_t count;
    uint8_t results[DICE_MAX_COUNT];
    uint8_t frames_left;
    bool rolled;
} DiceModel;

static uint8_t dice_roll_one(uint8_t sides) {
    return (furi_hal_random_get() % sides) + 1;
}

static void dice_roll_all(DiceModel* model) {
    uint8_t sides = dice_sides[model->sides_index];
    for(uint8_t i = 0; i < model->count; i++) {
        model->results[i] = dice_roll_one(sides);
    }
    model->rolled = true;
}

static void dice_draw_callback(Canvas* canvas, void* _model) {
    DiceModel* model = _model;
    char buf[24];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    snprintf(buf, sizeof(buf), "%ud%u", model->count, dice_sides[model->sides_index]);
    canvas_draw_str(canvas, 2, 10, "Dice");
    canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignBottom, buf);

    if(!model->rolled) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignCenter, "Up/Down: sides");
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "Left/Right: count");
    } else {
        // Draw every die as a framed box with its value
        const uint8_t box_w = 26;
        const uint8_t gap = 4;
        uint8_t total_w = model->count * box_w + (model->count - 1) * gap;
        uint8_t x = (128 - total_w) / 2;
        uint16_t total = 0;
        canvas_set_font(canvas, FontPrimary);
        for(uint8_t i = 0; i < model->count; i++) {
            elements_frame(canvas, x, 14, box_w, 18);
            snprintf(buf, sizeof(buf), "%u", model->results[i]);
            canvas_draw_str_aligned(canvas, x + box_w / 2, 23, AlignCenter, AlignCenter, buf);
            total += model->results[i];
            x += box_w + gap;
        }
        if(model->frames_left == 0 && model->count > 1) {
            canvas_set_font(canvas, FontSecondary);
            snprintf(buf, sizeof(buf), "Total: %u", total);
            canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignCenter, buf);
        }
    }

    elements_button_center(canvas, "Roll");
}

static bool dice_input_callback(InputEvent* event, void* context) {
    Dice* instance = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    bool consumed = true;
    bool start_roll = false;
    with_view_model(
        instance->view,
        DiceModel * model,
        {
            if(model->frames_left > 0) {
                // Ignore input while the dice are still tumbling
            } else if(event->key == InputKeyUp) {
                model->sides_index = (model->sides_index + 1) % DICE_SIDES_COUNT;
                model->rolled = false;
            } else if(event->key == InputKeyDown) {
                model->sides_index =
                    (model->sides_index + DICE_SIDES_COUNT - 1) % DICE_SIDES_COUNT;
                model->rolled = false;
            } else if(event->key == InputKeyRight) {
                if(model->count < DICE_MAX_COUNT) model->count++;
                model->rolled = false;
            } else if(event->key == InputKeyLeft) {
                if(model->count > 1) model->count--;
                model->rolled = false;
            } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
                model->frames_left = DICE_ANIMATION_FRAMES;
                dice_roll_all(model);
                start_roll = true;
            } else {
                consumed = false;
            }
        },
        consumed);

    if(start_roll) {
        furi_timer_start(instance->timer, furi_ms_to_ticks(DICE_ANIMATION_MS));
    }
    return consumed;
}

static void dice_timer_callback(void* context) {
    Dice* instance = context;
    bool finished = false;
    with_view_model(
        instance->view,
        DiceModel * model,
        {
            if(model->frames_left > 0) {
                model->frames_left--;
                dice_roll_all(model);
            }
            finished = model->frames_left == 0;
        },
        true);

    if(finished) {
        furi_timer_stop(instance->timer);
        notification_message(instance->notifications, &sequence_single_vibro);
    }
}

static void dice_enter_callback(void* context) {
    Dice* instance = context;
    FlipperOsSettings* settings = instance->settings;
    with_view_model(
        instance->view,
        DiceModel * model,
        {
            model->sides_index = settings->dice_sides_index < DICE_SIDES_COUNT ?
                                     settings->dice_sides_index :
                                     1;
            model->count = settings->dice_count >= 1 && settings->dice_count <= DICE_MAX_COUNT ?
                               settings->dice_count :
                               1;
        },
        true);
}

static void dice_exit_callback(void* context) {
    Dice* instance = context;
    furi_timer_stop(instance->timer);
    with_view_model(
        instance->view,
        DiceModel * model,
        {
            model->frames_left = 0;
            instance->settings->dice_sides_index = model->sides_index;
            instance->settings->dice_count = model->count;
        },
        false);
}

Dice* dice_alloc(FlipperOsSettings* settings) {
    Dice* instance = malloc(sizeof(Dice));
    instance->settings = settings;
    instance->notifications = furi_record_open(RECORD_NOTIFICATION);
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(DiceModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, dice_draw_callback);
    view_set_input_callback(instance->view, dice_input_callback);
    view_set_enter_callback(instance->view, dice_enter_callback);
    view_set_exit_callback(instance->view, dice_exit_callback);
    instance->timer = furi_timer_alloc(dice_timer_callback, FuriTimerTypePeriodic, instance);
    return instance;
}

void dice_free(Dice* instance) {
    furi_assert(instance);
    furi_timer_free(instance->timer);
    view_free(instance->view);
    furi_record_close(RECORD_NOTIFICATION);
    free(instance);
}

View* dice_get_view(Dice* instance) {
    return instance->view;
}
