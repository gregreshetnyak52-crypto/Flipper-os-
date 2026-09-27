#pragma once

#include <gui/view.h>
#include "../settings.h"

typedef struct Pomodoro Pomodoro;

Pomodoro* pomodoro_alloc(FlipperOsSettings* settings);
void pomodoro_free(Pomodoro* instance);
View* pomodoro_get_view(Pomodoro* instance);

/** Called when a phase ends while the pomodoro view is not on screen. */
void pomodoro_set_event_callback(
    Pomodoro* instance,
    FlipperOsEventCallback callback,
    void* context);
