#pragma once

#include <gui/view.h>

typedef struct Stopwatch Stopwatch;

Stopwatch* stopwatch_alloc(void);
void stopwatch_free(Stopwatch* instance);
View* stopwatch_get_view(Stopwatch* instance);
