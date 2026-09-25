#pragma once

#include <gui/view.h>

typedef struct SysInfo SysInfo;

SysInfo* sysinfo_alloc(void);
void sysinfo_free(SysInfo* instance);
View* sysinfo_get_view(SysInfo* instance);
