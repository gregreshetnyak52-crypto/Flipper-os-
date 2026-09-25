#include "sysinfo.h"

#include <furi.h>
#include <furi_hal_power.h>
#include <furi_hal_rtc.h>
#include <furi_hal_version.h>

#define SYSINFO_REFRESH_MS 1000

struct SysInfo {
    View* view;
    FuriTimer* timer;
};

typedef struct {
    uint8_t battery_pct;
    float voltage;
    float current;
    float temperature;
    bool charging;
    size_t free_heap;
    uint32_t uptime_s;
    DateTime datetime;
} SysInfoModel;

static void sysinfo_update(SysInfoModel* model) {
    model->battery_pct = furi_hal_power_get_pct();
    model->voltage = furi_hal_power_get_battery_voltage(FuriHalPowerICFuelGauge);
    model->current = furi_hal_power_get_battery_current(FuriHalPowerICFuelGauge);
    model->temperature = furi_hal_power_get_battery_temperature(FuriHalPowerICFuelGauge);
    model->charging = furi_hal_power_is_charging();
    model->free_heap = memmgr_get_free_heap();
    model->uptime_s = furi_get_tick() / furi_kernel_get_tick_frequency();
    furi_hal_rtc_get_datetime(&model->datetime);
}

static void sysinfo_draw_callback(Canvas* canvas, void* _model) {
    SysInfoModel* model = _model;
    char buf[40];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    const char* name = furi_hal_version_get_name_ptr();
    snprintf(buf, sizeof(buf), "Flipper: %s", name ? name : "Unknown");
    canvas_draw_str(canvas, 2, 10, buf);

    canvas_set_font(canvas, FontSecondary);
    snprintf(
        buf,
        sizeof(buf),
        "Battery: %u%% %s",
        model->battery_pct,
        model->charging ? "(charging)" : "");
    canvas_draw_str(canvas, 2, 21, buf);

    // Integer formatting keeps us independent of printf float support
    int32_t mv = (int32_t)(model->voltage * 1000.0f);
    int32_t ma = (int32_t)(model->current * 1000.0f);
    snprintf(buf, sizeof(buf), "%ld.%02ldV  %ldmA", mv / 1000, (mv % 1000) / 10, ma);
    canvas_draw_str(canvas, 2, 31, buf);

    snprintf(
        buf,
        sizeof(buf),
        "Temp: %dC  Heap: %uK",
        (int)model->temperature,
        (unsigned)(model->free_heap / 1024));
    canvas_draw_str(canvas, 2, 41, buf);

    uint32_t h = model->uptime_s / 3600;
    uint32_t m = (model->uptime_s / 60) % 60;
    uint32_t s = model->uptime_s % 60;
    snprintf(buf, sizeof(buf), "Uptime: %02lu:%02lu:%02lu", h, m, s);
    canvas_draw_str(canvas, 2, 51, buf);

    snprintf(
        buf,
        sizeof(buf),
        "%04u-%02u-%02u  %02u:%02u:%02u",
        model->datetime.year,
        model->datetime.month,
        model->datetime.day,
        model->datetime.hour,
        model->datetime.minute,
        model->datetime.second);
    canvas_draw_str(canvas, 2, 61, buf);
}

static void sysinfo_timer_callback(void* context) {
    SysInfo* instance = context;
    with_view_model(instance->view, SysInfoModel * model, { sysinfo_update(model); }, true);
}

static void sysinfo_enter_callback(void* context) {
    SysInfo* instance = context;
    sysinfo_timer_callback(instance);
    furi_timer_start(instance->timer, furi_ms_to_ticks(SYSINFO_REFRESH_MS));
}

static void sysinfo_exit_callback(void* context) {
    SysInfo* instance = context;
    furi_timer_stop(instance->timer);
}

SysInfo* sysinfo_alloc(FlipperOsSettings* settings) {
    UNUSED(settings);
    SysInfo* instance = malloc(sizeof(SysInfo));
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(SysInfoModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, sysinfo_draw_callback);
    view_set_enter_callback(instance->view, sysinfo_enter_callback);
    view_set_exit_callback(instance->view, sysinfo_exit_callback);
    instance->timer = furi_timer_alloc(sysinfo_timer_callback, FuriTimerTypePeriodic, instance);
    return instance;
}

void sysinfo_free(SysInfo* instance) {
    furi_assert(instance);
    furi_timer_free(instance->timer);
    view_free(instance->view);
    free(instance);
}

View* sysinfo_get_view(SysInfo* instance) {
    return instance->view;
}
