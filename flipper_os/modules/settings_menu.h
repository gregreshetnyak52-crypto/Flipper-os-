#pragma once

#include <gui/view.h>
#include "../settings.h"

typedef struct SettingsMenu SettingsMenu;

SettingsMenu* settings_menu_alloc(FlipperOsSettings* settings);
void settings_menu_free(SettingsMenu* instance);
View* settings_menu_get_view(SettingsMenu* instance);
