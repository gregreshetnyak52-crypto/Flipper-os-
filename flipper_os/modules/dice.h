#pragma once

#include <gui/view.h>

typedef struct Dice Dice;

Dice* dice_alloc(void);
void dice_free(Dice* instance);
View* dice_get_view(Dice* instance);
