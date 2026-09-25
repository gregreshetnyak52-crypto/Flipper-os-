#pragma once

#include <gui/view.h>
#include "../settings.h"

typedef struct Flashlight Flashlight;

Flashlight* flashlight_alloc(FlipperOsSettings* settings);
void flashlight_free(Flashlight* instance);
View* flashlight_get_view(Flashlight* instance);
