#pragma once

#include <gui/view.h>
#include "../settings.h"

typedef struct Countdown Countdown;

Countdown* countdown_alloc(FlipperOsSettings* settings);
void countdown_free(Countdown* instance);
View* countdown_get_view(Countdown* instance);

/** Called when the countdown goes off while its view is not on screen. */
void countdown_set_event_callback(
    Countdown* instance,
    FlipperOsEventCallback callback,
    void* context);
