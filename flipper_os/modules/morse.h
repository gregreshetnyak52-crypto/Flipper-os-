#pragma once

#include <gui/view.h>

typedef struct Morse Morse;

Morse* morse_alloc(void);
void morse_free(Morse* instance);
View* morse_get_view(Morse* instance);
