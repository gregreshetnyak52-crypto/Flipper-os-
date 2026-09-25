#pragma once

#include <gui/view.h>
#include "../settings.h"

typedef struct Dice Dice;

Dice* dice_alloc(FlipperOsSettings* settings);
void dice_free(Dice* instance);
View* dice_get_view(Dice* instance);
