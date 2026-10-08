// ui_apps internals: the clock app screens and helpers they share.
#pragma once

#include <stddef.h>
#include <time.h>

#include "clock_apps.h"
#include "shell_priv.h"
#include "world_clock.h"

extern const screen_def_t clock_alarms_screen;
extern const screen_def_t clock_alarm_edit_screen;
extern const screen_def_t clock_ring_screen;
extern const screen_def_t clock_timer_screen;
extern const screen_def_t clock_timer_custom_screen;
extern const screen_def_t clock_stopwatch_screen;
extern const screen_def_t clock_world_screen;
extern const screen_def_t clock_world_add_screen;

#define CLOCK_ORANGE 0xFF9F0A // alarm/timer/stopwatch app colour (launcher)

/** The backend; every member is set (stubs when none was installed). */
const clock_backend_t *clock_backend(void);

/** Rebuild an open screen after clock_apps_changed(). */
void clock_alarms_refresh(ui_screen_t *s);
void clock_timer_refresh(ui_screen_t *s);

/** The ring screen s is gone or closing (its own button, a pop). */
void clock_ring_closed(const ui_screen_t *s);

/** Transparent container without style, sized to content. */
lv_obj_t *clock_plain(lv_obj_t *parent);
/** Round button d px across, colour fill, text in font. Listen for LV_EVENT_CLICKED. */
lv_obj_t *clock_round_button(lv_obj_t *parent, int32_t d, uint32_t color, const char *text, const lv_font_t *font);

/** "07:05" (24 h) or "7:05 AM" (12 h, ui_clock_is_24h()). */
void clock_fmt_hm(int hour, int minute, char *buf, size_t len);
/** Time left, rounded up to the second: "04:59", "1:00:00". */
void clock_fmt_duration(uint32_t ms, char *buf, size_t len);
/** "in 8 h 51 min", "in 5 min", "in under a minute". */
void clock_fmt_until(int64_t secs, char *buf, size_t len);

/** Cities shown now (up to WORLD_CLOCK_MAX), and saving a new list (listener). */
size_t clock_world_cities(const world_city_t **out);
void clock_world_save(const world_city_t *const *cities, size_t n);
/** Local - UTC offset of the home zone at now (newlib localtime). */
int32_t clock_home_offset(time_t now);
