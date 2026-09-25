#pragma once

#include <gui/view.h>

typedef struct Snake Snake;

Snake* snake_alloc(void);
void snake_free(Snake* instance);
View* snake_get_view(Snake* instance);
