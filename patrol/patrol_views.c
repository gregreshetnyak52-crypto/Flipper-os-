#include "patrol_views.h"

#include <furi.h>
#include <gui/elements.h>

#define PATROL_TEXT_LEN 32

// ---- Round -------------------------------------------------------------------

struct PatrolRoundView {
    View* view;
    FuriTimer* clock; // redraws the elapsed time
    PatrolActionCallback callback;
    void* context;
};

typedef struct {
    uint32_t round;
    uint32_t start_tick;
    uint8_t visited;
    uint8_t total;
    uint8_t issues;
    bool all_done;
    char next_name[PATROL_NAME_LEN + 1];
    PatrolTech next_tech;
    PatrolTech radio;
    PatrolMark mark;
    char last[PATROL_TEXT_LEN];
} PatrolRoundModel;

static void patrol_round_draw(Canvas* canvas, void* _model) {
    PatrolRoundModel* model = _model;
    char buf[40];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    snprintf(buf, sizeof(buf), "Round %lu", (unsigned long)model->round);
    canvas_draw_str(canvas, 1, 9, buf);

    canvas_set_font(canvas, FontSecondary);
    uint32_t seconds = (furi_get_tick() - model->start_tick) / furi_kernel_get_tick_frequency();
    snprintf(
        buf,
        sizeof(buf),
        "%lu:%02lu",
        (unsigned long)(seconds / 60),
        (unsigned long)(seconds % 60));
    canvas_draw_str_aligned(canvas, 127, 9, AlignRight, AlignBottom, buf);
    snprintf(buf, sizeof(buf), "%u/%u", model->visited, model->total);
    canvas_draw_str_aligned(canvas, 86, 9, AlignRight, AlignBottom, buf);

    elements_progress_bar(
        canvas, 1, 12, 126, model->total ? (float)model->visited / model->total : 0.0f);

    if(model->all_done) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 1, 32, "All checkpoints done");
        canvas_set_font(canvas, FontSecondary);
    } else {
        snprintf(buf, sizeof(buf), "Next: %s", model->next_name);
        canvas_draw_str(canvas, 1, 31, buf);
        canvas_draw_str_aligned(
            canvas, 127, 31, AlignRight, AlignBottom, patrol_tech_name(model->next_tech));
    }

    if(model->mark != PatrolMarkNone) {
        const char* prefix = model->mark == PatrolMarkOk      ? "OK" :
                             model->mark == PatrolMarkWarning ? "??" :
                                                                "!!";
        // Errors stand out inverted
        if(model->mark == PatrolMarkError) {
            canvas_draw_box(canvas, 0, 34, 128, 11);
            canvas_set_color(canvas, ColorWhite);
        }
        snprintf(buf, sizeof(buf), "%s %s", prefix, model->last);
        canvas_draw_str(canvas, 1, 43, buf);
        canvas_set_color(canvas, ColorBlack);
    }

    // Current radio, inverted when it does not match the next checkpoint
    snprintf(buf, sizeof(buf), "<%s>", patrol_tech_name(model->radio));
    uint16_t width = canvas_string_width(canvas, buf) + 3;
    bool mismatch = !model->all_done && model->radio != model->next_tech;
    if(mismatch) {
        canvas_draw_box(canvas, 0, 54, width, 10);
        canvas_set_color(canvas, ColorWhite);
    }
    canvas_draw_str(canvas, 1, 62, buf);
    canvas_set_color(canvas, ColorBlack);

    snprintf(buf, sizeof(buf), "^Issue %u", model->issues);
    canvas_draw_str_aligned(canvas, 127, 62, AlignRight, AlignBottom, buf);
    elements_button_center(canvas, "Hold:End");
}

static bool patrol_round_input(InputEvent* event, void* context) {
    PatrolRoundView* instance = context;
    if(!instance->callback) return false;

    if(event->key == InputKeyBack) {
        // Leaving a round always goes through the finish confirmation
        if(event->type == InputTypeShort) {
            instance->callback(instance->context, PatrolActionFinish);
        }
        return true;
    }
    if(event->key == InputKeyOk && event->type == InputTypeLong) {
        instance->callback(instance->context, PatrolActionFinish);
    } else if(event->type == InputTypeShort) {
        if(event->key == InputKeyRight) {
            instance->callback(instance->context, PatrolActionRadioNext);
        } else if(event->key == InputKeyLeft) {
            instance->callback(instance->context, PatrolActionRadioPrev);
        } else if(event->key == InputKeyUp) {
            instance->callback(instance->context, PatrolActionIssue);
        }
    }
    return true;
}

static void patrol_round_clock(void* context) {
    PatrolRoundView* instance = context;
    with_view_model(instance->view, PatrolRoundModel * model, { UNUSED(model); }, true);
}

static void patrol_round_enter(void* context) {
    PatrolRoundView* instance = context;
    furi_timer_start(instance->clock, furi_ms_to_ticks(1000));
}

static void patrol_round_exit(void* context) {
    PatrolRoundView* instance = context;
    furi_timer_stop(instance->clock);
}

PatrolRoundView* patrol_round_view_alloc(void) {
    PatrolRoundView* instance = malloc(sizeof(PatrolRoundView));
    instance->callback = NULL;
    instance->context = NULL;
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(PatrolRoundModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, patrol_round_draw);
    view_set_input_callback(instance->view, patrol_round_input);
    view_set_enter_callback(instance->view, patrol_round_enter);
    view_set_exit_callback(instance->view, patrol_round_exit);
    instance->clock = furi_timer_alloc(patrol_round_clock, FuriTimerTypePeriodic, instance);
    return instance;
}

void patrol_round_view_free(PatrolRoundView* instance) {
    furi_timer_stop(instance->clock);
    furi_timer_free(instance->clock);
    view_free(instance->view);
    free(instance);
}

View* patrol_round_view_get_view(PatrolRoundView* instance) {
    return instance->view;
}

void patrol_round_view_set_callback(
    PatrolRoundView* instance,
    PatrolActionCallback callback,
    void* context) {
    instance->callback = callback;
    instance->context = context;
}

void patrol_round_view_start(PatrolRoundView* instance, uint32_t round, uint8_t total) {
    with_view_model(
        instance->view,
        PatrolRoundModel * model,
        {
            memset(model, 0, sizeof(PatrolRoundModel));
            model->round = round;
            model->total = total;
            model->start_tick = furi_get_tick();
        },
        true);
}

void patrol_round_view_set_progress(
    PatrolRoundView* instance,
    uint8_t visited,
    uint8_t issues,
    const char* next_name,
    PatrolTech next_tech) {
    with_view_model(
        instance->view,
        PatrolRoundModel * model,
        {
            model->visited = visited;
            model->issues = issues;
            model->all_done = next_name == NULL;
            strlcpy(model->next_name, next_name ? next_name : "", sizeof(model->next_name));
            model->next_tech = next_tech;
        },
        true);
}

void patrol_round_view_set_radio(PatrolRoundView* instance, PatrolTech radio) {
    with_view_model(instance->view, PatrolRoundModel * model, { model->radio = radio; }, true);
}

void patrol_round_view_set_last(PatrolRoundView* instance, PatrolMark mark, const char* text) {
    with_view_model(
        instance->view,
        PatrolRoundModel * model,
        {
            model->mark = mark;
            strlcpy(model->last, text, sizeof(model->last));
        },
        true);
}

// ---- Scan --------------------------------------------------------------------

struct PatrolScanView {
    View* view;
    PatrolActionCallback callback;
    void* context;
};

typedef struct {
    PatrolTech radio;
    char status[PATROL_TEXT_LEN];
} PatrolScanModel;

static void patrol_scan_draw(Canvas* canvas, void* _model) {
    PatrolScanModel* model = _model;
    char buf[40];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 1, 9, "Add checkpoint");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 1, 22, "Hold the tag to the back");
    canvas_draw_str(canvas, 1, 32, "of the Flipper.");

    snprintf(buf, sizeof(buf), "< %s >", patrol_tech_name(model->radio));
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 45, AlignCenter, AlignBottom, buf);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas,
        64,
        60,
        AlignCenter,
        AlignBottom,
        model->status[0] ? model->status : "Left/Right: tag type");
}

static bool patrol_scan_input(InputEvent* event, void* context) {
    PatrolScanView* instance = context;
    if(event->type != InputTypeShort || !instance->callback) return false;
    if(event->key == InputKeyRight) {
        instance->callback(instance->context, PatrolActionRadioNext);
        return true;
    }
    if(event->key == InputKeyLeft) {
        instance->callback(instance->context, PatrolActionRadioPrev);
        return true;
    }
    return false;
}

PatrolScanView* patrol_scan_view_alloc(void) {
    PatrolScanView* instance = malloc(sizeof(PatrolScanView));
    instance->callback = NULL;
    instance->context = NULL;
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(PatrolScanModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, patrol_scan_draw);
    view_set_input_callback(instance->view, patrol_scan_input);
    return instance;
}

void patrol_scan_view_free(PatrolScanView* instance) {
    view_free(instance->view);
    free(instance);
}

View* patrol_scan_view_get_view(PatrolScanView* instance) {
    return instance->view;
}

void patrol_scan_view_set_callback(
    PatrolScanView* instance,
    PatrolActionCallback callback,
    void* context) {
    instance->callback = callback;
    instance->context = context;
}

void patrol_scan_view_set_radio(PatrolScanView* instance, PatrolTech radio) {
    with_view_model(instance->view, PatrolScanModel * model, { model->radio = radio; }, true);
}

void patrol_scan_view_set_status(PatrolScanView* instance, const char* status) {
    with_view_model(
        instance->view,
        PatrolScanModel * model,
        { strlcpy(model->status, status, sizeof(model->status)); },
        true);
}

// ---- PIN ---------------------------------------------------------------------

struct PatrolPinView {
    View* view;
    PatrolPinCallback callback;
    void* context;
};

typedef struct {
    char prompt[PATROL_TEXT_LEN];
    char entry[PATROL_PIN_LEN + 1];
} PatrolPinModel;

static void patrol_pin_draw(Canvas* canvas, void* _model) {
    PatrolPinModel* model = _model;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 12, AlignCenter, AlignBottom, model->prompt);

    // Four slots, filled as arrows are pressed (never show which arrow)
    size_t filled = strlen(model->entry);
    for(size_t i = 0; i < PATROL_PIN_LEN; i++) {
        uint8_t x = 25 + i * 22;
        if(i < filled) {
            canvas_draw_box(canvas, x, 24, 14, 14);
        } else {
            canvas_draw_frame(canvas, x, 24, 14, 14);
        }
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignBottom, "Use the 4 arrow keys");
}

static bool patrol_pin_input(InputEvent* event, void* context) {
    PatrolPinView* instance = context;
    if(event->type != InputTypeShort) return false;

    char symbol = 0;
    switch(event->key) {
    case InputKeyUp:
        symbol = 'U';
        break;
    case InputKeyDown:
        symbol = 'D';
        break;
    case InputKeyLeft:
        symbol = 'L';
        break;
    case InputKeyRight:
        symbol = 'R';
        break;
    default:
        return false;
    }

    char complete[PATROL_PIN_LEN + 1] = {0};
    with_view_model(
        instance->view,
        PatrolPinModel * model,
        {
            size_t len = strlen(model->entry);
            model->entry[len] = symbol;
            model->entry[len + 1] = '\0';
            if(len + 1 == PATROL_PIN_LEN) {
                strlcpy(complete, model->entry, sizeof(complete));
                model->entry[0] = '\0';
            }
        },
        true);

    if(complete[0] && instance->callback) instance->callback(instance->context, complete);
    return true;
}

PatrolPinView* patrol_pin_view_alloc(void) {
    PatrolPinView* instance = malloc(sizeof(PatrolPinView));
    instance->callback = NULL;
    instance->context = NULL;
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(PatrolPinModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, patrol_pin_draw);
    view_set_input_callback(instance->view, patrol_pin_input);
    return instance;
}

void patrol_pin_view_free(PatrolPinView* instance) {
    view_free(instance->view);
    free(instance);
}

View* patrol_pin_view_get_view(PatrolPinView* instance) {
    return instance->view;
}

void patrol_pin_view_set_callback(PatrolPinView* instance, PatrolPinCallback callback, void* context) {
    instance->callback = callback;
    instance->context = context;
}

void patrol_pin_view_reset(PatrolPinView* instance, const char* prompt) {
    with_view_model(
        instance->view,
        PatrolPinModel * model,
        {
            strlcpy(model->prompt, prompt, sizeof(model->prompt));
            model->entry[0] = '\0';
        },
        true);
}
