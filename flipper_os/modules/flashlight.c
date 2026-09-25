#include "flashlight.h"

#include <furi.h>
#include <gui/elements.h>
#include <notification/notification_messages.h>

#define FLASHLIGHT_LEVELS 4
#define FLASHLIGHT_STROBE_MS 80
#define FLASHLIGHT_BEACON_ON_MS 100
#define FLASHLIGHT_BEACON_PERIOD_MS 1500

typedef enum {
    FlashlightModeSteady,
    FlashlightModeStrobe,
    FlashlightModeBeacon,
    FlashlightModeCount,
} FlashlightMode;

static const char* const flashlight_mode_names[FlashlightModeCount] = {
    "Steady",
    "Strobe",
    "Beacon",
};

static const uint8_t flashlight_level_pct[FLASHLIGHT_LEVELS] = {25, 50, 75, 100};

// Notification messages are processed asynchronously, so every message we
// send must have static storage: one white LED triplet per brightness level.
#define FLASHLIGHT_LED(t, v) {.type = t, .data.led.value = v}
static const NotificationMessage flashlight_red[FLASHLIGHT_LEVELS] = {
    FLASHLIGHT_LED(NotificationMessageTypeLedRed, 0x40),
    FLASHLIGHT_LED(NotificationMessageTypeLedRed, 0x80),
    FLASHLIGHT_LED(NotificationMessageTypeLedRed, 0xC0),
    FLASHLIGHT_LED(NotificationMessageTypeLedRed, 0xFF),
};
static const NotificationMessage flashlight_green[FLASHLIGHT_LEVELS] = {
    FLASHLIGHT_LED(NotificationMessageTypeLedGreen, 0x40),
    FLASHLIGHT_LED(NotificationMessageTypeLedGreen, 0x80),
    FLASHLIGHT_LED(NotificationMessageTypeLedGreen, 0xC0),
    FLASHLIGHT_LED(NotificationMessageTypeLedGreen, 0xFF),
};
static const NotificationMessage flashlight_blue[FLASHLIGHT_LEVELS] = {
    FLASHLIGHT_LED(NotificationMessageTypeLedBlue, 0x40),
    FLASHLIGHT_LED(NotificationMessageTypeLedBlue, 0x80),
    FLASHLIGHT_LED(NotificationMessageTypeLedBlue, 0xC0),
    FLASHLIGHT_LED(NotificationMessageTypeLedBlue, 0xFF),
};

#define FLASHLIGHT_SEQUENCE(l) \
    {&flashlight_red[l], &flashlight_green[l], &flashlight_blue[l], &message_do_not_reset, NULL}
static const NotificationSequence flashlight_on_25 = FLASHLIGHT_SEQUENCE(0);
static const NotificationSequence flashlight_on_50 = FLASHLIGHT_SEQUENCE(1);
static const NotificationSequence flashlight_on_75 = FLASHLIGHT_SEQUENCE(2);
static const NotificationSequence flashlight_on_100 = FLASHLIGHT_SEQUENCE(3);
static const NotificationSequence* const flashlight_on_sequences[FLASHLIGHT_LEVELS] = {
    &flashlight_on_25,
    &flashlight_on_50,
    &flashlight_on_75,
    &flashlight_on_100,
};

static const NotificationSequence flashlight_off_sequence = {
    &message_red_0,
    &message_green_0,
    &message_blue_0,
    &message_do_not_reset,
    NULL,
};

struct Flashlight {
    View* view;
    FuriTimer* timer;
    NotificationApp* notifications;
    FlipperOsSettings* settings;
    uint32_t phase_ms;
};

typedef struct {
    bool on;
    uint8_t level;
    FlashlightMode mode;
} FlashlightModel;

static void flashlight_led(Flashlight* instance, bool on, uint8_t level) {
    notification_message(
        instance->notifications, on ? flashlight_on_sequences[level] : &flashlight_off_sequence);
}

/** Apply the current model to the hardware. Call with the model locked. */
static void flashlight_apply(Flashlight* instance, FlashlightModel* model) {
    furi_timer_stop(instance->timer);
    instance->phase_ms = 0;
    if(!model->on) {
        flashlight_led(instance, false, 0);
        notification_message(instance->notifications, &sequence_display_backlight_enforce_auto);
        notification_message(instance->notifications, &sequence_reset_rgb);
        return;
    }

    notification_message(instance->notifications, &sequence_display_backlight_enforce_on);
    if(model->mode == FlashlightModeSteady) {
        flashlight_led(instance, true, model->level);
    } else {
        uint32_t period = model->mode == FlashlightModeStrobe ? FLASHLIGHT_STROBE_MS :
                                                                FLASHLIGHT_BEACON_ON_MS;
        flashlight_led(instance, true, model->level);
        furi_timer_start(instance->timer, furi_ms_to_ticks(period));
    }
}

static void flashlight_timer_callback(void* context) {
    Flashlight* instance = context;
    with_view_model(
        instance->view,
        FlashlightModel * model,
        {
            if(model->on) {
                if(model->mode == FlashlightModeStrobe) {
                    // Toggle every tick: 50% duty cycle
                    instance->phase_ms += FLASHLIGHT_STROBE_MS;
                    bool lit = (instance->phase_ms / FLASHLIGHT_STROBE_MS) % 2 == 0;
                    flashlight_led(instance, lit, model->level);
                } else if(model->mode == FlashlightModeBeacon) {
                    // Short flash once per period
                    instance->phase_ms =
                        (instance->phase_ms + FLASHLIGHT_BEACON_ON_MS) % FLASHLIGHT_BEACON_PERIOD_MS;
                    flashlight_led(instance, instance->phase_ms == 0, model->level);
                }
            }
        },
        false);
}

static void flashlight_draw_callback(Canvas* canvas, void* _model) {
    FlashlightModel* model = _model;
    char buf[24];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Flashlight");
    snprintf(buf, sizeof(buf), "< %s >", flashlight_mode_names[model->mode]);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignBottom, buf);

    // Bulb: filled when on
    const uint8_t cx = 30;
    const uint8_t cy = 30;
    if(model->on) {
        canvas_draw_disc(canvas, cx, cy, 10);
        for(uint8_t i = 0; i < 8; i++) {
            static const int8_t rays[8][2] = {
                {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};
            canvas_draw_line(
                canvas,
                cx + rays[i][0] * 13,
                cy + rays[i][1] * 13,
                cx + rays[i][0] * 16,
                cy + rays[i][1] * 16);
        }
    } else {
        canvas_draw_circle(canvas, cx, cy, 10);
    }

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 60, 26, model->on ? "ON" : "OFF");
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "Brightness %u%%", flashlight_level_pct[model->level]);
    canvas_draw_str(canvas, 60, 38, buf);

    elements_button_center(canvas, model->on ? "Off" : "On");
}

static bool flashlight_input_callback(InputEvent* event, void* context) {
    Flashlight* instance = context;
    if(event->type != InputTypeShort) return false;

    bool consumed = true;
    with_view_model(
        instance->view,
        FlashlightModel * model,
        {
            if(event->key == InputKeyOk) {
                model->on = !model->on;
            } else if(event->key == InputKeyUp) {
                if(model->level < FLASHLIGHT_LEVELS - 1) model->level++;
            } else if(event->key == InputKeyDown) {
                if(model->level > 0) model->level--;
            } else if(event->key == InputKeyRight) {
                model->mode = (model->mode + 1) % FlashlightModeCount;
            } else if(event->key == InputKeyLeft) {
                model->mode = (model->mode + FlashlightModeCount - 1) % FlashlightModeCount;
            } else {
                consumed = false;
            }
            if(consumed) flashlight_apply(instance, model);
        },
        consumed);
    return consumed;
}

static void flashlight_exit_callback(void* context) {
    Flashlight* instance = context;
    // Never leave the light burning once the user leaves the screen
    with_view_model(
        instance->view,
        FlashlightModel * model,
        {
            model->on = false;
            flashlight_apply(instance, model);
            instance->settings->flashlight_level = model->level;
            instance->settings->flashlight_mode = model->mode;
        },
        false);
}

Flashlight* flashlight_alloc(FlipperOsSettings* settings) {
    Flashlight* instance = malloc(sizeof(Flashlight));
    instance->settings = settings;
    instance->phase_ms = 0;
    instance->notifications = furi_record_open(RECORD_NOTIFICATION);
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(FlashlightModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, flashlight_draw_callback);
    view_set_input_callback(instance->view, flashlight_input_callback);
    view_set_exit_callback(instance->view, flashlight_exit_callback);
    instance->timer =
        furi_timer_alloc(flashlight_timer_callback, FuriTimerTypePeriodic, instance);

    with_view_model(
        instance->view,
        FlashlightModel * model,
        {
            model->on = false;
            model->level = settings->flashlight_level < FLASHLIGHT_LEVELS ?
                               settings->flashlight_level :
                               FLASHLIGHT_LEVELS - 1;
            model->mode = settings->flashlight_mode < FlashlightModeCount ?
                              settings->flashlight_mode :
                              FlashlightModeSteady;
        },
        false);
    return instance;
}

void flashlight_free(Flashlight* instance) {
    furi_assert(instance);
    furi_timer_free(instance->timer);
    view_free(instance->view);
    furi_record_close(RECORD_NOTIFICATION);
    free(instance);
}

View* flashlight_get_view(Flashlight* instance) {
    return instance->view;
}
