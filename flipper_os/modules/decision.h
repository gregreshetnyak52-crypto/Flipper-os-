#pragma once

#include <gui/view.h>
#include "../settings.h"

typedef struct Decision Decision;

Decision* decision_alloc(FlipperOsSettings* settings);
void decision_free(Decision* instance);
View* decision_get_view(Decision* instance);
