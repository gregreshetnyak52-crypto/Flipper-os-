#pragma once

#include <gui/view.h>
#include "../settings.h"

typedef struct Counter Counter;

Counter* counter_alloc(FlipperOsSettings* settings);
void counter_free(Counter* instance);
View* counter_get_view(Counter* instance);
