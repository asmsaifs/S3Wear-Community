// World clock cities (docs/03-firmware-features.md F6). Pure logic, no LVGL: built
// by firmware/host_test. A built-in list of cities with POSIX TZ strings (tz_posix.h)
// until the phone sends its city list (P4); the user's choice is the WORLD_CLOCKS
// setting, a comma-separated list of city ids.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WORLD_CLOCK_MAX 6

typedef struct {
    const char *id;    // stable, stored in WORLD_CLOCKS
    const char *name;  // "New York"
    const char *label; // complication caption, <= 7 upper-case letters ("NYC")
    const char *tz;    // POSIX TZ
} world_city_t;

size_t world_city_count(void);
const world_city_t *world_city_at(size_t index);
const world_city_t *world_city_find(const char *id);

/** Cities in a WORLD_CLOCKS string: unknown ids and repeats skipped, at most max. */
size_t world_clock_parse(const char *csv, const world_city_t **out, size_t max);
/** The ids joined by commas into buf. */
void world_clock_join(const world_city_t *const *cities, size_t n, char *buf, size_t len);

/** UTC offset (s east) of the city at utc; 0 if its TZ does not parse. */
int32_t world_city_offset(const world_city_t *c, int64_t utc);

/** "Today, +3 h", "Tomorrow, +5:30 h", "Yesterday, -9 h", "Today, same time": the
 *  city's day and offset relative to home, both as offsets east of UTC. */
void world_clock_relative(int32_t city_off, int32_t home_off, int64_t utc, char *buf, size_t len);

#ifdef __cplusplus
}
#endif
