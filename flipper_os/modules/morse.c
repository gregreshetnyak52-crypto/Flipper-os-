#include "morse.h"

#include <furi.h>
#include <gui/elements.h>
#include <notification/notification_messages.h>

#define MORSE_MAX_UNITS 256
#define MORSE_UNIT_MS 150

typedef enum {
    MorseOutputLed,
    MorseOutputLedVibro,
    MorseOutputLedSound,
    MorseOutputCount,
} MorseOutput;

static const char* const morse_output_names[MorseOutputCount] = {
    "LED",
    "LED+Vibro",
    "LED+Sound",
};

static const char* const morse_messages[] = {
    "SOS",
    "HELLO WORLD",
    "FLIPPER ZERO",
    "CQ CQ CQ",
    "73",
};
#define MORSE_MESSAGES_COUNT COUNT_OF(morse_messages)

// Morse alphabet: A-Z followed by 0-9
static const char* const morse_letters[26] = {
    ".-",   "-...", "-.-.", "-..",  ".",   "..-.", "--.",  "....", "..",
    ".---", "-.-",  ".-..", "--",   "-.",  "---",  ".--.", "--.-", ".-.",
    "...",  "-",    "..-",  "...-", ".--", "-..-", "-.--", "--..",
};
static const char* const morse_digits[10] = {
    "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----.",
};

static const NotificationSequence morse_on_led = {
    &message_red_255,
    &message_green_255,
    &message_blue_255,
    &message_do_not_reset,
    NULL,
};
static const NotificationSequence morse_on_vibro = {
    &message_red_255,
    &message_green_255,
    &message_blue_255,
    &message_vibro_on,
    &message_do_not_reset,
    NULL,
};
static const NotificationSequence morse_on_sound = {
    &message_red_255,
    &message_green_255,
    &message_blue_255,
    &message_note_a5,
    &message_do_not_reset,
    NULL,
};
static const NotificationSequence morse_off = {
    &message_red_0,
    &message_green_0,
    &message_blue_0,
    &message_vibro_off,
    &message_sound_off,
    &message_do_not_reset,
    NULL,
};

struct Morse {
    View* view;
    FuriTimer* timer;
    NotificationApp* notifications;
    FlipperOsSettings* settings;
    // Pre-rendered on/off pattern, one entry per time unit
    bool units[MORSE_MAX_UNITS];
    uint16_t units_count;
    bool output_on;
};

typedef struct {
    uint8_t message_index;
    MorseOutput output;
    bool playing;
    bool loop;
    uint16_t position;
    uint16_t length;
} MorseModel;

static const char* morse_code_for(char c) {
    if(c >= 'A' && c <= 'Z') return morse_letters[c - 'A'];
    if(c >= '0' && c <= '9') return morse_digits[c - '0'];
    return NULL;
}

static void morse_push(Morse* instance, bool on, uint8_t units) {
    for(uint8_t i = 0; i < units && instance->units_count < MORSE_MAX_UNITS; i++) {
        instance->units[instance->units_count++] = on;
    }
}

static void morse_encode(Morse* instance, const char* text) {
    instance->units_count = 0;
    for(const char* p = text; *p; p++) {
        if(*p == ' ') {
            // Word gap is 7 units; 3 were already added after the previous letter
            morse_push(instance, false, 4);
            continue;
        }
        const char* code = morse_code_for(*p);
        if(!code) continue;
        for(const char* s = code; *s; s++) {
            morse_push(instance, true, *s == '-' ? 3 : 1);
            morse_push(instance, false, 1);
        }
        // Letter gap is 3 units; 1 was already added after the last symbol
        morse_push(instance, false, 2);
    }
    // Pause before the message repeats
    morse_push(instance, false, 4);
}

static void morse_set_output(Morse* instance, bool on, MorseOutput output) {
    if(on == instance->output_on) return;
    instance->output_on = on;
    if(!on) {
        notification_message(instance->notifications, &morse_off);
    } else if(output == MorseOutputLedVibro) {
        notification_message(instance->notifications, &morse_on_vibro);
    } else if(output == MorseOutputLedSound) {
        notification_message(instance->notifications, &morse_on_sound);
    } else {
        notification_message(instance->notifications, &morse_on_led);
    }
}

// Must be called after model->playing was cleared, so the timer stops emitting
static void morse_stop(Morse* instance) {
    furi_timer_stop(instance->timer);
    instance->output_on = false;
    notification_message(instance->notifications, &morse_off);
    notification_message(instance->notifications, &sequence_reset_rgb);
}

static void morse_draw_callback(Canvas* canvas, void* _model) {
    MorseModel* model = _model;
    char buf[32];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Morse Beacon");

    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "< %s >", morse_messages[model->message_index]);
    canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignCenter, buf);
    snprintf(
        buf,
        sizeof(buf),
        "Out: %s  %s",
        morse_output_names[model->output],
        model->loop ? "Loop" : "Once");
    canvas_draw_str_aligned(canvas, 64, 33, AlignCenter, AlignCenter, buf);

    if(model->playing && model->length > 0) {
        elements_progress_bar(canvas, 4, 40, 120, (float)model->position / model->length);
    } else {
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, "Up: output  Down: loop");
    }

    elements_button_center(canvas, model->playing ? "Stop" : "Play");
}

static bool morse_input_callback(InputEvent* event, void* context) {
    Morse* instance = context;
    if(event->type != InputTypeShort) return false;

    bool consumed = true;
    bool start = false;
    bool stop = false;
    with_view_model(
        instance->view,
        MorseModel * model,
        {
            if(event->key == InputKeyOk) {
                if(model->playing) {
                    model->playing = false;
                    stop = true;
                } else {
                    morse_encode(instance, morse_messages[model->message_index]);
                    model->position = 0;
                    model->length = instance->units_count;
                    model->playing = true;
                    start = true;
                }
            } else if(model->playing) {
                // Settings are locked while transmitting
                consumed = event->key != InputKeyBack;
            } else if(event->key == InputKeyRight) {
                model->message_index = (model->message_index + 1) % MORSE_MESSAGES_COUNT;
            } else if(event->key == InputKeyLeft) {
                model->message_index =
                    (model->message_index + MORSE_MESSAGES_COUNT - 1) % MORSE_MESSAGES_COUNT;
            } else if(event->key == InputKeyUp) {
                model->output = (model->output + 1) % MorseOutputCount;
            } else if(event->key == InputKeyDown) {
                model->loop = !model->loop;
            } else {
                consumed = false;
            }
        },
        consumed);

    if(stop) morse_stop(instance);
    if(start) furi_timer_start(instance->timer, furi_ms_to_ticks(MORSE_UNIT_MS));
    return consumed;
}

static void morse_timer_callback(void* context) {
    Morse* instance = context;
    bool finished = false;

    // Output is switched while holding the model lock so that a concurrent
    // stop request can never be overtaken by a late "on" message.
    with_view_model(
        instance->view,
        MorseModel * model,
        {
            if(model->playing && model->position >= model->length) {
                if(model->loop) {
                    model->position = 0;
                } else {
                    model->playing = false;
                    finished = true;
                }
            }
            if(model->playing) {
                morse_set_output(instance, instance->units[model->position++], model->output);
            }
        },
        true);

    if(finished) morse_stop(instance);
}

static void morse_exit_callback(void* context) {
    Morse* instance = context;
    with_view_model(
        instance->view,
        MorseModel * model,
        {
            model->playing = false;
            instance->settings->morse_message = model->message_index;
            instance->settings->morse_output = model->output;
            instance->settings->morse_loop = model->loop;
        },
        false);
    morse_stop(instance);
}

Morse* morse_alloc(FlipperOsSettings* settings) {
    Morse* instance = malloc(sizeof(Morse));
    instance->settings = settings;
    instance->notifications = furi_record_open(RECORD_NOTIFICATION);
    instance->units_count = 0;
    instance->output_on = false;
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(MorseModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, morse_draw_callback);
    view_set_input_callback(instance->view, morse_input_callback);
    view_set_exit_callback(instance->view, morse_exit_callback);
    instance->timer = furi_timer_alloc(morse_timer_callback, FuriTimerTypePeriodic, instance);

    with_view_model(
        instance->view,
        MorseModel * model,
        {
            model->message_index =
                settings->morse_message < MORSE_MESSAGES_COUNT ? settings->morse_message : 0;
            model->output =
                settings->morse_output < MorseOutputCount ? settings->morse_output : MorseOutputLed;
            model->loop = settings->morse_loop;
        },
        false);
    return instance;
}

void morse_free(Morse* instance) {
    furi_assert(instance);
    furi_timer_free(instance->timer);
    view_free(instance->view);
    furi_record_close(RECORD_NOTIFICATION);
    free(instance);
}

View* morse_get_view(Morse* instance) {
    return instance->view;
}
