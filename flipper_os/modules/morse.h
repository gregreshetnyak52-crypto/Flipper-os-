#pragma once

#include <gui/view.h>
#include "../settings.h"

typedef struct Morse Morse;

Morse* morse_alloc(FlipperOsSettings* settings);
void morse_free(Morse* instance);
View* morse_get_view(Morse* instance);

/** Receives FlipperOsEventEditMorseText when the user wants to edit the custom text. */
void morse_set_event_callback(Morse* instance, FlipperOsEventCallback callback, void* context);
