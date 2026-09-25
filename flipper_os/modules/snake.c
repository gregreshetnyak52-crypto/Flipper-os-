#include "snake.h"

#include <furi.h>
#include <furi_hal_random.h>
#include <notification/notification_messages.h>

#define SNAKE_CELL 4
#define SNAKE_COLS 32
#define SNAKE_ROWS 14
#define SNAKE_TOP 8
#define SNAKE_MAX_LEN (SNAKE_COLS * SNAKE_ROWS)
#define SNAKE_START_LEN 3
#define SNAKE_SPEED_START_MS 200
#define SNAKE_SPEED_MIN_MS 70

typedef enum {
    SnakeDirUp,
    SnakeDirRight,
    SnakeDirDown,
    SnakeDirLeft,
} SnakeDir;

typedef enum {
    SnakeStateReady,
    SnakeStatePlaying,
    SnakeStatePaused,
    SnakeStateGameOver,
} SnakeState;

typedef struct {
    uint8_t x;
    uint8_t y;
} SnakePoint;

struct Snake {
    View* view;
    FuriTimer* timer;
    NotificationApp* notifications;
};

typedef struct {
    SnakePoint body[SNAKE_MAX_LEN]; // body[0] is the head
    uint16_t length;
    SnakePoint food;
    SnakeDir dir;
    SnakeDir next_dir;
    SnakeState state;
    uint16_t score;
    uint16_t best;
} SnakeModel;

static uint32_t snake_period_ms(const SnakeModel* model) {
    int32_t period = SNAKE_SPEED_START_MS - (int32_t)model->score * 5;
    return period < SNAKE_SPEED_MIN_MS ? SNAKE_SPEED_MIN_MS : (uint32_t)period;
}

static bool snake_occupies(const SnakeModel* model, SnakePoint p, uint16_t upto) {
    for(uint16_t i = 0; i < upto; i++) {
        if(model->body[i].x == p.x && model->body[i].y == p.y) return true;
    }
    return false;
}

static void snake_place_food(SnakeModel* model) {
    // Pick a random free cell by index among the remaining free cells
    uint16_t free_cells = SNAKE_MAX_LEN - model->length;
    if(free_cells == 0) return;
    uint16_t target = furi_hal_random_get() % free_cells;
    for(uint8_t y = 0; y < SNAKE_ROWS; y++) {
        for(uint8_t x = 0; x < SNAKE_COLS; x++) {
            SnakePoint p = {x, y};
            if(snake_occupies(model, p, model->length)) continue;
            if(target-- == 0) {
                model->food = p;
                return;
            }
        }
    }
}

static void snake_reset(SnakeModel* model) {
    model->length = SNAKE_START_LEN;
    for(uint16_t i = 0; i < model->length; i++) {
        model->body[i].x = SNAKE_COLS / 2 - i;
        model->body[i].y = SNAKE_ROWS / 2;
    }
    model->dir = SnakeDirRight;
    model->next_dir = SnakeDirRight;
    model->score = 0;
    snake_place_food(model);
}

/** Advance the game by one step. Returns true if the snake ate food. */
static bool snake_step(SnakeModel* model) {
    model->dir = model->next_dir;
    SnakePoint head = model->body[0];
    switch(model->dir) {
    case SnakeDirUp:
        head.y = (head.y + SNAKE_ROWS - 1) % SNAKE_ROWS;
        break;
    case SnakeDirDown:
        head.y = (head.y + 1) % SNAKE_ROWS;
        break;
    case SnakeDirLeft:
        head.x = (head.x + SNAKE_COLS - 1) % SNAKE_COLS;
        break;
    case SnakeDirRight:
        head.x = (head.x + 1) % SNAKE_COLS;
        break;
    }

    bool ate = head.x == model->food.x && head.y == model->food.y;
    // The tail moves away this step unless we grow, so it is not an obstacle
    uint16_t check_len = ate ? model->length : model->length - 1;
    if(snake_occupies(model, head, check_len)) {
        model->state = SnakeStateGameOver;
        if(model->score > model->best) model->best = model->score;
        return false;
    }

    if(ate && model->length < SNAKE_MAX_LEN) model->length++;
    memmove(&model->body[1], &model->body[0], sizeof(SnakePoint) * (model->length - 1));
    model->body[0] = head;

    if(ate) {
        model->score++;
        snake_place_food(model);
    }
    return ate;
}

static void snake_draw_callback(Canvas* canvas, void* _model) {
    SnakeModel* model = _model;
    char buf[32];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "Score: %u", model->score);
    canvas_draw_str(canvas, 1, 7, buf);
    snprintf(buf, sizeof(buf), "Best: %u", model->best);
    canvas_draw_str_aligned(canvas, 127, 7, AlignRight, AlignBottom, buf);

    for(uint16_t i = 0; i < model->length; i++) {
        canvas_draw_box(
            canvas,
            model->body[i].x * SNAKE_CELL,
            SNAKE_TOP + model->body[i].y * SNAKE_CELL,
            SNAKE_CELL - (i == 0 ? 0 : 1),
            SNAKE_CELL - (i == 0 ? 0 : 1));
    }
    canvas_draw_frame(
        canvas,
        model->food.x * SNAKE_CELL,
        SNAKE_TOP + model->food.y * SNAKE_CELL,
        SNAKE_CELL,
        SNAKE_CELL);

    const char* message = NULL;
    if(model->state == SnakeStateReady) message = "Press OK to start";
    if(model->state == SnakeStatePaused) message = "Paused - OK";
    if(model->state == SnakeStateGameOver) message = "Game over - OK";
    if(message) {
        canvas_set_font(canvas, FontPrimary);
        uint16_t w = canvas_string_width(canvas, message) + 8;
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 64 - w / 2, 26, w, 14);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, 64 - w / 2, 26, w, 14);
        canvas_draw_str_aligned(canvas, 64, 33, AlignCenter, AlignCenter, message);
    }
}

static bool snake_input_callback(InputEvent* event, void* context) {
    Snake* instance = context;
    if(event->type != InputTypePress && event->type != InputTypeShort) return false;
    if(event->key == InputKeyBack) return false;

    bool start_timer = false;
    bool stop_timer = false;
    uint32_t period = 0;
    with_view_model(
        instance->view,
        SnakeModel * model,
        {
            if(event->key == InputKeyOk) {
                if(event->type == InputTypeShort) {
                    if(model->state == SnakeStatePlaying) {
                        model->state = SnakeStatePaused;
                        stop_timer = true;
                    } else {
                        if(model->state != SnakeStatePaused) snake_reset(model);
                        model->state = SnakeStatePlaying;
                        start_timer = true;
                        period = snake_period_ms(model);
                    }
                }
            } else if(event->type == InputTypePress && model->state == SnakeStatePlaying) {
                // React on press (not release) so steering feels responsive
                SnakeDir dir = model->dir;
                if(event->key == InputKeyUp) dir = SnakeDirUp;
                if(event->key == InputKeyDown) dir = SnakeDirDown;
                if(event->key == InputKeyLeft) dir = SnakeDirLeft;
                if(event->key == InputKeyRight) dir = SnakeDirRight;
                // Forbid reversing into ourselves
                if((dir + 2) % 4 != model->dir) model->next_dir = dir;
            }
        },
        true);

    if(stop_timer) furi_timer_stop(instance->timer);
    if(start_timer) furi_timer_start(instance->timer, furi_ms_to_ticks(period));
    return true;
}

static void snake_timer_callback(void* context) {
    Snake* instance = context;
    bool ate = false;
    bool over = false;
    uint32_t period = 0;
    with_view_model(
        instance->view,
        SnakeModel * model,
        {
            if(model->state == SnakeStatePlaying) {
                ate = snake_step(model);
                over = model->state == SnakeStateGameOver;
                period = snake_period_ms(model);
            }
        },
        true);

    if(over) {
        furi_timer_stop(instance->timer);
        notification_message(instance->notifications, &sequence_error);
    } else if(ate) {
        notification_message(instance->notifications, &sequence_single_vibro);
        // Speed up as the snake grows
        furi_timer_start(instance->timer, furi_ms_to_ticks(period));
    }
}

static void snake_exit_callback(void* context) {
    Snake* instance = context;
    furi_timer_stop(instance->timer);
    with_view_model(
        instance->view,
        SnakeModel * model,
        {
            if(model->state == SnakeStatePlaying) model->state = SnakeStatePaused;
        },
        false);
}

Snake* snake_alloc(void) {
    Snake* instance = malloc(sizeof(Snake));
    instance->notifications = furi_record_open(RECORD_NOTIFICATION);
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(SnakeModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, snake_draw_callback);
    view_set_input_callback(instance->view, snake_input_callback);
    view_set_exit_callback(instance->view, snake_exit_callback);
    instance->timer = furi_timer_alloc(snake_timer_callback, FuriTimerTypePeriodic, instance);

    with_view_model(
        instance->view,
        SnakeModel * model,
        {
            model->best = 0;
            model->state = SnakeStateReady;
            snake_reset(model);
        },
        false);
    return instance;
}

void snake_free(Snake* instance) {
    furi_assert(instance);
    furi_timer_free(instance->timer);
    view_free(instance->view);
    furi_record_close(RECORD_NOTIFICATION);
    free(instance);
}

View* snake_get_view(Snake* instance) {
    return instance->view;
}
