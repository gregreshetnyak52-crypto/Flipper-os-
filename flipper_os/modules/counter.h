#pragma once

#include <gui/view.h>

typedef struct Counter Counter;

Counter* counter_alloc(void);
void counter_free(Counter* instance);
View* counter_get_view(Counter* instance);
