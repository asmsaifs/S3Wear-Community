// Edition flag (docs/10-editions-licensing.md §3): S3W_EDITION_PRO is 1 in the Pro edition,
// 0 in the Community edition. Test it with #if, never #ifdef. The watch build takes it from
// CONFIG_S3W_EDITION_PRO; the simulator from its S3W_EDITION_PRO CMake option; other host
// builds (host tests) are Pro unless they define it.
#pragma once

#ifndef S3W_EDITION_PRO
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#ifdef CONFIG_S3W_EDITION_PRO
#define S3W_EDITION_PRO 1
#else
#define S3W_EDITION_PRO 0
#endif
#else
#define S3W_EDITION_PRO 1
#endif
#endif
