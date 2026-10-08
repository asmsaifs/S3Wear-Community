// ui_framework internals shared between nav.c and overlay.c.
#pragma once

#include <stdbool.h>

/** overlay.c -> nav.c: a full-screen alert covers (or uncovers) the screen stack. */
void ui_nav_set_covered(bool covered);

/** nav.c -> overlay.c: BACK pressed. True if an alert consumed it. */
bool ui_alert_handle_back(void);

/** True if the top screen has UI_SCREEN_FULLSCREEN (banners are suppressed). */
bool ui_nav_top_is_fullscreen(void);
