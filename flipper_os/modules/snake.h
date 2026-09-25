#pragma once

#include <gui/view.h>
#include "../settings.h"

typedef struct Snake Snake;

Snake* snake_alloc(FlipperOsSettings* settings);
void snake_free(Snake* instance);
View* snake_get_view(Snake* instance);
