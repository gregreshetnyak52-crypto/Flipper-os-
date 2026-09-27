#include "password.h"

#include <furi.h>
#include <furi_hal_random.h>
#include <gui/elements.h>

#define PASSWORD_LEN_MIN 4
#define PASSWORD_LEN_MAX 32
#define PASSWORD_LINE_CHARS 16

typedef enum {
    PasswordCharsetPin,
    PasswordCharsetLower,
    PasswordCharsetMixed,
    PasswordCharsetSymbols,
    PasswordCharsetCount,
} PasswordCharset;

static const char password_digits[] = "0123456789";
static const char password_lower[] = "abcdefghijklmnopqrstuvwxyz";
static const char password_upper[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char password_symbols[] = "!#$%&*+-=?@^_~";

static const struct {
    const char* name;
    // Character classes; the password contains at least one of each
    const char* classes[4];
    uint8_t classes_count;
    // log2 of the alphabet size, in hundredths of a bit
    uint16_t bits_x100;
} password_charsets[PasswordCharsetCount] = {
    {"0-9", {password_digits}, 1, 332},
    {"a-z0-9", {password_lower, password_digits}, 2, 517},
    {"Aa0-9", {password_lower, password_upper, password_digits}, 3, 595},
    {"Aa0-9#$", {password_lower, password_upper, password_digits, password_symbols}, 4, 641},
};

struct Password {
    View* view;
    FlipperOsSettings* settings;
};

typedef struct {
    uint8_t length;
    PasswordCharset charset;
    char text[PASSWORD_LEN_MAX + 1];
} PasswordModel;

/** Uniform random number in [0, bound) without modulo bias. */
static uint32_t password_random_below(uint32_t bound) {
    uint32_t limit = UINT32_MAX - UINT32_MAX % bound;
    uint32_t value;
    do {
        value = furi_hal_random_get();
    } while(value >= limit);
    return value % bound;
}

static void password_generate(PasswordModel* model) {
    const char* classes[4];
    size_t sizes[4];
    size_t alphabet = 0;
    uint8_t count = password_charsets[model->charset].classes_count;
    for(uint8_t i = 0; i < count; i++) {
        classes[i] = password_charsets[model->charset].classes[i];
        sizes[i] = strlen(classes[i]);
        alphabet += sizes[i];
    }

    // Draw from the whole alphabet and retry until every class is present.
    // Each class is likely enough that this ends after a few tries.
    bool complete = false;
    while(!complete) {
        bool present[4] = {false};
        for(uint8_t i = 0; i < model->length; i++) {
            size_t index = password_random_below(alphabet);
            uint8_t c = 0;
            while(index >= sizes[c]) index -= sizes[c++];
            model->text[i] = classes[c][index];
            present[c] = true;
        }
        model->text[model->length] = '\0';
        complete = true;
        for(uint8_t c = 0; c < count; c++) complete &= present[c];
    }
}

static void password_draw_callback(Canvas* canvas, void* _model) {
    PasswordModel* model = _model;
    char buf[24];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Password");
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "< %s >", password_charsets[model->charset].name);
    canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignBottom, buf);

    // Monospace, split into lines so that every character stays readable
    canvas_set_font(canvas, FontKeyboard);
    uint8_t lines = (model->length + PASSWORD_LINE_CHARS - 1) / PASSWORD_LINE_CHARS;
    for(uint8_t line = 0; line < lines; line++) {
        char part[PASSWORD_LINE_CHARS + 1];
        strlcpy(part, model->text + line * PASSWORD_LINE_CHARS, sizeof(part));
        canvas_draw_str_aligned(
            canvas, 64, (lines == 1 ? 26 : 21) + line * 11, AlignCenter, AlignCenter, part);
    }

    canvas_set_font(canvas, FontSecondary);
    uint32_t bits = (uint32_t)model->length * password_charsets[model->charset].bits_x100 / 100;
    snprintf(buf, sizeof(buf), "%u chars  ~%lu bits", model->length, (unsigned long)bits);
    canvas_draw_str_aligned(canvas, 64, 45, AlignCenter, AlignCenter, buf);

    elements_button_center(canvas, "New");
}

static bool password_input_callback(InputEvent* event, void* context) {
    Password* instance = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    bool consumed = true;
    with_view_model(
        instance->view,
        PasswordModel * model,
        {
            if(event->key == InputKeyUp) {
                if(model->length < PASSWORD_LEN_MAX) model->length++;
            } else if(event->key == InputKeyDown) {
                if(model->length > PASSWORD_LEN_MIN) model->length--;
            } else if(event->key == InputKeyRight && event->type == InputTypeShort) {
                model->charset = (model->charset + 1) % PasswordCharsetCount;
            } else if(event->key == InputKeyLeft && event->type == InputTypeShort) {
                model->charset = (model->charset + PasswordCharsetCount - 1) % PasswordCharsetCount;
            } else if(event->key != InputKeyOk || event->type != InputTypeShort) {
                consumed = false;
            }
            // Every change makes a fresh password
            if(consumed) password_generate(model);
        },
        consumed);
    return consumed;
}

static void password_enter_callback(void* context) {
    Password* instance = context;
    FlipperOsSettings* settings = instance->settings;
    with_view_model(
        instance->view,
        PasswordModel * model,
        {
            model->length = CLAMP(settings->password_length, PASSWORD_LEN_MAX, PASSWORD_LEN_MIN);
            model->charset = settings->password_charset < PasswordCharsetCount ?
                                 settings->password_charset :
                                 PasswordCharsetMixed;
            password_generate(model);
        },
        true);
}

static void password_exit_callback(void* context) {
    Password* instance = context;
    with_view_model(
        instance->view,
        PasswordModel * model,
        {
            instance->settings->password_length = model->length;
            instance->settings->password_charset = model->charset;
            // Do not keep the secret around in memory
            memset(model->text, 0, sizeof(model->text));
        },
        false);
}

Password* password_alloc(FlipperOsSettings* settings) {
    Password* instance = malloc(sizeof(Password));
    instance->settings = settings;
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(PasswordModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, password_draw_callback);
    view_set_input_callback(instance->view, password_input_callback);
    view_set_enter_callback(instance->view, password_enter_callback);
    view_set_exit_callback(instance->view, password_exit_callback);
    return instance;
}

void password_free(Password* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* password_get_view(Password* instance) {
    return instance->view;
}
