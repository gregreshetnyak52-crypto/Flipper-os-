#pragma once

#include <gui/view.h>
#include "../settings.h"

typedef struct Stopwatch Stopwatch;

Stopwatch* stopwatch_alloc(FlipperOsSettings* settings);
void stopwatch_free(Stopwatch* instance);
View* stopwatch_get_view(Stopwatch* instance);
