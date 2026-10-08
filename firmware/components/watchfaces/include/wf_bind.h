// Binding registry (docs/03-firmware-features.md F1): the names a face element binds
// to ("time.hh:mm", "date.EEE d MMM", "steps.goal_ratio", ...). Faces cannot run code;
// they can only show these values. Native faces use the same formatters, and the
// declarative face loader (wf_decl.h) resolves face.json "bind" strings with
// wf_bind_parse(). Pure C, no LVGL.
//
// | Binding                 | Text                    | Value                              |
// |-------------------------|-------------------------|------------------------------------|
// | time.hh:mm              | clock, 12/24 h, "--:--" | minutes since midnight             |
// | time.hh / time.mm       | "10" / "09"             | hour (12 or 24 h) / minute         |
// | time.ss                 | "05"                    | second                             |
// | time.ampm               | "AM"/"PM", "" in 24 h   | 0 / 1                              |
// | time.hour/minute/second | (none)                  | hand angle, 0.1° clockwise from 12 |
// | date.<pattern>          | pattern, see below      | day of month                       |
// | battery.percent         | "82"                    | 82                                 |
// | battery.ratio           | "82%"                   | 0..1000                            |
// | battery.charging        | "" or charging symbol   | 0 / 1                              |
// | steps.count / .goal     | "6,420" / "10,000"      | count / goal                       |
// | steps.goal_ratio        | "64%"                   | 0..1000 (capped)                   |
// | weather.temp/.low/.high | "18°"                   | °C                                 |
// | weather.condition       | "Cloudy"                | wf_weather_t                       |
// | phone.battery           | "64"                    | 64                                 |
// | next_event.title        | "Standup"               | (none)                             |
// | next_event.time         | "10:30" (12/24 h)       | minutes since midnight             |
// | heart.bpm               | "72"                    | 72                                 |
// | notifications.count     | "3"                     | 3                                  |
// | moon.phase              | "Waxing gibbous"        | cycle 0..1000 (0 new, 500 full)    |
// | moon.illumination       | "73%"                   | 0..1000                            |
//
// Date patterns: EEEE (Saturday), EEE (Sat), d (3), dd (03), MMMM (October), MMM (Oct),
// MM (10), M (10), yyyy (2026), yy (26); letters a-z/A-Z outside these are invalid,
// anything else is copied. English names (LANGUAGE "en"; other languages later).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wf_data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WF_BIND_PATTERN_MAX 24

typedef struct {
    uint8_t key;                       // internal key index
    char pattern[WF_BIND_PATTERN_MAX]; // date.<pattern> only
} wf_bind_t;

/** Parse a binding name. False if unknown or the date pattern is invalid/too long. */
bool wf_bind_parse(const char *name, wf_bind_t *out);

/** Text form into buf (always NUL-terminated). False (and "--" or "--:--") if the
 *  data is unknown; time and date bindings are unknown while the time is not set. */
bool wf_bind_text(const wf_bind_t *b, const wf_ctx_t *ctx, char *buf, size_t len);

/** Numeric form (table above). False if unknown or the binding has no value. */
bool wf_bind_value(const wf_bind_t *b, const wf_ctx_t *ctx, int32_t *out);

/** Whether the binding has a text form (not the hand angles) / a numeric form (not
 *  next_event.title), per the table above. */
bool wf_bind_has_text(const wf_bind_t *b);
bool wf_bind_has_value(const wf_bind_t *b);

/** wf_data_mask_t bits after which the binding must be re-read (WF_DATA_SECOND only
 *  for second-resolution bindings). */
uint32_t wf_bind_deps(const wf_bind_t *b);

/** Format tm with a date pattern (rules above). False if the pattern is invalid. */
bool wf_format_date(const char *pattern, const struct tm *tm, char *buf, size_t len);

/** Clock text for a local time: "09:05" (24 h) or "9:05" (12 h). */
void wf_format_hm(int hour, int minute, bool h24, char *buf, size_t len);

/** Integer with thousands separators: 6420 -> "6,420". */
void wf_format_thousands(int32_t v, char *buf, size_t len);

/** "Clear", "Cloudy", ... ("" if out of range). */
const char *wf_weather_name(uint8_t weather);

/** Moon cycle position at t: 0..999 (0 new moon, 500 full), mean synodic month. */
int32_t wf_moon_cycle(time_t t);
/** Lit fraction of the disc for a cycle position, 0..1000. */
int32_t wf_moon_illumination(int32_t cycle);
/** "New moon", "Waxing crescent", ... for a cycle position. */
const char *wf_moon_phase_name(int32_t cycle);

#ifdef __cplusplus
}
#endif
