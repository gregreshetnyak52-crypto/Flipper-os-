#include "stopwatch.h"

#include <furi.h>
#include <gui/elements.h>

#define STOPWATCH_LAPS_MAX 50
#define STOPWATCH_LAPS_VISIBLE 2
#define STOPWATCH_REFRESH_MS 50

struct Stopwatch {
    View* view;
    FuriTimer* timer;
};

typedef struct {
    bool running;
    uint32_t start_tick;
    uint32_t accumulated_ms;
    // Elapsed time at each lap mark; once full the oldest lap is dropped
    uint32_t laps[STOPWATCH_LAPS_MAX];
    uint8_t lap_count;
    uint16_t lap_dropped; // number of the oldest stored lap minus one
    uint8_t scroll; // 0 shows the newest laps
} StopwatchModel;

static uint32_t stopwatch_ticks_to_ms(uint32_t ticks) {
    // Stay in 32-bit arithmetic: 64-bit division helpers are not exported to FAPs
    uint32_t freq = furi_kernel_get_tick_frequency();
    return ticks / freq * 1000 + (ticks % freq) * 1000 / freq;
}

static uint32_t stopwatch_elapsed_ms(const StopwatchModel* model) {
    uint32_t elapsed = model->accumulated_ms;
    if(model->running) {
        elapsed += stopwatch_ticks_to_ms(furi_get_tick() - model->start_tick);
    }
    return elapsed;
}

static void stopwatch_format(char* buf, size_t size, uint32_t ms) {
    uint32_t minutes = ms / 60000;
    uint32_t seconds = (ms / 1000) % 60;
    uint32_t hundredths = (ms / 10) % 100;
    snprintf(
        buf, size, "%02lu:%02lu.%02lu", (unsigned long)minutes, (unsigned long)seconds,
        (unsigned long)hundredths);
}

// Callers skip index 0 once older laps were dropped: its split is unknown
static uint32_t stopwatch_lap_split(const StopwatchModel* model, uint8_t index) {
    return index == 0 ? model->laps[0] : model->laps[index] - model->laps[index - 1];
}

// Compact m:ss.hh form for the lap list
static void stopwatch_format_short(char* buf, size_t size, uint32_t ms) {
    snprintf(
        buf,
        size,
        "%lu:%02lu.%02lu",
        (unsigned long)(ms / 60000),
        (unsigned long)((ms / 1000) % 60),
        (unsigned long)((ms / 10) % 100));
}

static void stopwatch_draw_callback(Canvas* canvas, void* _model) {
    StopwatchModel* model = _model;
    char buf[24];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Stopwatch");

    stopwatch_format(buf, sizeof(buf), stopwatch_elapsed_ms(model));
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignCenter, buf);

    canvas_set_font(canvas, FontSecondary);
    if(model->lap_count > 0) {
        // Fastest lap, ignoring the first stored one if its split is unknown
        uint8_t fastest = model->lap_dropped ? 1 : 0;
        for(uint8_t i = fastest + 1; i < model->lap_count; i++) {
            if(stopwatch_lap_split(model, i) < stopwatch_lap_split(model, fastest)) fastest = i;
        }

        // Newest first, scrolled by model->scroll
        for(uint8_t row = 0; row < STOPWATCH_LAPS_VISIBLE; row++) {
            if(model->scroll + row >= model->lap_count) break;
            uint8_t i = model->lap_count - 1 - model->scroll - row;
            char split[24];
            char line[64];
            stopwatch_format_short(buf, sizeof(buf), model->laps[i]);
            if(i == 0 && model->lap_dropped) {
                snprintf(line, sizeof(line), "#%u %s", model->lap_dropped + i + 1, buf);
            } else {
                stopwatch_format_short(split, sizeof(split), stopwatch_lap_split(model, i));
                snprintf(
                    line,
                    sizeof(line),
                    "%s#%u %s +%s",
                    i == fastest && model->lap_count > 1 ? "*" : "",
                    model->lap_dropped + i + 1,
                    buf,
                    split);
            }
            canvas_draw_str_aligned(canvas, 62, 34 + row * 9, AlignCenter, AlignTop, line);
        }
        if(model->lap_count > STOPWATCH_LAPS_VISIBLE) {
            elements_scrollbar_pos(
                canvas,
                127,
                33,
                18,
                model->scroll,
                model->lap_count - STOPWATCH_LAPS_VISIBLE + 1);
        }
    }

    elements_button_center(canvas, model->running ? "Stop" : "Start");
    elements_button_left(canvas, model->running ? "Lap" : "Reset");
}

static bool stopwatch_input_callback(InputEvent* event, void* context) {
    Stopwatch* instance = context;
    bool repeat = event->type == InputTypeRepeat &&
                  (event->key == InputKeyUp || event->key == InputKeyDown);
    if(event->type != InputTypeShort && !repeat) return false;

    bool consumed = false;
    with_view_model(
        instance->view,
        StopwatchModel * model,
        {
            if(event->key == InputKeyOk) {
                if(model->running) {
                    model->accumulated_ms = stopwatch_elapsed_ms(model);
                    model->running = false;
                } else {
                    model->start_tick = furi_get_tick();
                    model->running = true;
                }
                consumed = true;
            } else if(event->key == InputKeyLeft) {
                if(model->running) {
                    // Keep the most recent laps, dropping the oldest one
                    if(model->lap_count == STOPWATCH_LAPS_MAX) {
                        memmove(
                            model->laps, model->laps + 1,
                            sizeof(uint32_t) * (STOPWATCH_LAPS_MAX - 1));
                        model->lap_count--;
                        model->lap_dropped++;
                    }
                    model->laps[model->lap_count++] = stopwatch_elapsed_ms(model);
                } else {
                    model->accumulated_ms = 0;
                    model->lap_count = 0;
                    model->lap_dropped = 0;
                }
                model->scroll = 0;
                consumed = true;
            } else if(event->key == InputKeyUp) {
                if(model->scroll > 0) model->scroll--;
                consumed = true;
            } else if(event->key == InputKeyDown) {
                if(model->scroll + STOPWATCH_LAPS_VISIBLE < model->lap_count) model->scroll++;
                consumed = true;
            }
        },
        consumed);
    return consumed;
}

static void stopwatch_timer_callback(void* context) {
    Stopwatch* instance = context;
    bool running = false;
    with_view_model(instance->view, StopwatchModel * model, { running = model->running; }, false);
    if(running) {
        with_view_model(instance->view, StopwatchModel * model, { UNUSED(model); }, true);
    }
}

static void stopwatch_enter_callback(void* context) {
    Stopwatch* instance = context;
    furi_timer_start(instance->timer, furi_ms_to_ticks(STOPWATCH_REFRESH_MS));
}

static void stopwatch_exit_callback(void* context) {
    Stopwatch* instance = context;
    furi_timer_stop(instance->timer);
}

Stopwatch* stopwatch_alloc(FlipperOsSettings* settings) {
    UNUSED(settings);
    Stopwatch* instance = malloc(sizeof(Stopwatch));
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(StopwatchModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, stopwatch_draw_callback);
    view_set_input_callback(instance->view, stopwatch_input_callback);
    view_set_enter_callback(instance->view, stopwatch_enter_callback);
    view_set_exit_callback(instance->view, stopwatch_exit_callback);
    instance->timer = furi_timer_alloc(stopwatch_timer_callback, FuriTimerTypePeriodic, instance);
    return instance;
}

void stopwatch_free(Stopwatch* instance) {
    furi_assert(instance);
    furi_timer_free(instance->timer);
    view_free(instance->view);
    free(instance);
}

View* stopwatch_get_view(Stopwatch* instance) {
    return instance->view;
}
