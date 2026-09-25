#pragma once

#include <gui/view.h>
#include "../settings.h"

typedef struct Morse Morse;

Morse* morse_alloc(FlipperOsSettings* settings);
void morse_free(Morse* instance);
View* morse_get_view(Morse* instance);
