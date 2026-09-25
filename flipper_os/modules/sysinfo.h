#pragma once

#include <gui/view.h>
#include "../settings.h"

typedef struct SysInfo SysInfo;

SysInfo* sysinfo_alloc(FlipperOsSettings* settings);
void sysinfo_free(SysInfo* instance);
View* sysinfo_get_view(SysInfo* instance);
