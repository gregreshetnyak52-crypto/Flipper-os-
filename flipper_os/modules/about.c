#include "about.h"

#include <furi.h>
#include <furi_hal_version.h>
#include <toolbox/version.h>

struct About {
    View* view;
};

static void about_draw_callback(Canvas* canvas, void* _model) {
    UNUSED(_model);
    const Version* version = version_get();
    char line[40];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "About");

    canvas_set_font(canvas, FontSecondary);
    snprintf(line, sizeof(line), "Toolkit v" FLIPPER_OS_VERSION);
    canvas_draw_str(canvas, 2, 21, line);

    // Firmware origin is "FlipperOS" on our own build, "Unleashed" if this
    // .fap is instead installed on stock Unleashed, "Official" on mainline.
    snprintf(line, sizeof(line), "Firmware: %s", version_get_firmware_origin(version));
    canvas_draw_str(canvas, 2, 31, line);

    snprintf(
        line,
        sizeof(line),
        "%s @ %s%s",
        version_get_version(version),
        version_get_githash(version),
        version_get_dirty_flag(version) ? "-dirty" : "");
    canvas_draw_str(canvas, 2, 41, line);

    snprintf(line, sizeof(line), "Built: %s", version_get_builddate(version));
    canvas_draw_str(canvas, 2, 51, line);

    const char* model = furi_hal_version_get_model_name();
    snprintf(line, sizeof(line), "%s", model ? model : "Flipper Zero");
    canvas_draw_str(canvas, 2, 61, line);

    canvas_draw_str_aligned(
        canvas, 126, 61, AlignRight, AlignBottom, "github.com/gregreshetnyak52-crypto");
}

About* about_alloc(FlipperOsSettings* settings) {
    UNUSED(settings);
    About* instance = malloc(sizeof(About));
    instance->view = view_alloc();
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, about_draw_callback);
    return instance;
}

void about_free(About* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* about_get_view(About* instance) {
    return instance->view;
}
