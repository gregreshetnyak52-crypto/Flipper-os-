#pragma once

#include <gui/view.h>
#include "../settings.h"

typedef struct Reaction Reaction;

Reaction* reaction_alloc(FlipperOsSettings* settings);
void reaction_free(Reaction* instance);
View* reaction_get_view(Reaction* instance);
