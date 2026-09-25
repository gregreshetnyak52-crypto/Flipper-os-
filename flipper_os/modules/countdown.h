#pragma once

#include <gui/view.h>
#include "../settings.h"

typedef struct Countdown Countdown;

Countdown* countdown_alloc(FlipperOsSettings* settings);
void countdown_free(Countdown* instance);
View* countdown_get_view(Countdown* instance);
