#pragma once

#include <gui/view.h>
#include "../settings.h"

typedef struct About About;

About* about_alloc(FlipperOsSettings* settings);
void about_free(About* instance);
View* about_get_view(About* instance);
