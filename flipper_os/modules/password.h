#pragma once

#include <gui/view.h>
#include "../settings.h"

typedef struct Password Password;

Password* password_alloc(FlipperOsSettings* settings);
void password_free(Password* instance);
View* password_get_view(Password* instance);
