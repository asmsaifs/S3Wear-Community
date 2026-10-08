// Calendar app (agenda), and the next event of the watch faces, complications and the Next event
// tile (docs/03 F11, docs/04 §4j, P6-03). Portable (LVGL + ui_framework + watchfaces): the
// simulator builds it too. UI task only, like ui_nav.h.
//
// The agenda comes in with calendar_apps_set() (app_main: svc_calendar events; simulator:
// sim_calendar.c). From it, and again on every minute, the next-event fields of wf_data are
// recomputed with calendar_next(), so faces move on to the next event by themselves.
#pragma once

#include "calendar_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Register the screen and the minute update (shell_init() does this). */
void calendar_apps_init(void);

/** A new agenda (copied): update wf_data and the Calendar app (id "calendar") if open. */
void calendar_apps_set(const calendar_t *c);

#ifdef __cplusplus
}
#endif
