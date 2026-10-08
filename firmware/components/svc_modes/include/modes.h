// Do not disturb, sleep and theater modes with weekly schedules (docs/03 F2/F4/F7,
// P3-08). Pure logic, no ESP-IDF: built by firmware/host_test and the simulator.
// svc_modes.c owns the instance and feeds it the local time.
//
// Time is (wday, min): local weekday 0 = Sunday and minute of the day 0..1439.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MODE_DND,     // notifications silent and do not wake the screen
    MODE_SLEEP,   // DND + dark screen (no AOD, no tap or raise wake)
    MODE_THEATER, // as sleep, and the screen goes off at once; by hand only
    MODE_COUNT,
} mode_id_t;

#define MODES_MIN_PER_DAY  1440
#define MODES_MIN_PER_WEEK (7 * MODES_MIN_PER_DAY)

/** A weekly window. It starts at start_min on each day in days and ends at end_min,
 *  the next day if end_min <= start_min (end_min == start_min: 24 h). */
typedef struct {
    uint8_t days;       // bit 0 = Sunday .. bit 6 = Saturday; 0 = no schedule
    uint16_t start_min; // 0..1439
    uint16_t end_min;   // 0..1439, exclusive
} mode_sched_t;

typedef struct {
    bool manual[MODE_COUNT];        // turned on by hand
    bool skip[MODE_COUNT];          // turned off by hand inside its window: until the window ends
    mode_sched_t sched[MODE_COUNT]; // MODE_THEATER has none
} modes_t;

/** What the modes mean for the rest of the watch. */
typedef struct {
    bool dnd;     // each mode's own state
    bool sleep;
    bool theater;
    bool quiet;   // dnd || sleep || theater: notifications neither sound nor wake the screen
    bool dark;    // sleep || theater: no AOD, tap and raise do not wake (buttons, alarms, charger do)
} modes_state_t;

/** True if the window covers (wday, min). */
bool mode_sched_active(const mode_sched_t *s, int wday, int min);

/** Minutes from (wday, min) to the next time the window starts or ends (1..MODES_MIN_PER_WEEK),
 *  0 if it never changes (no days, or 24 h every day). */
uint32_t mode_sched_next_edge(const mode_sched_t *s, int wday, int min);

/** Re-evaluate at (wday, min): a skip ends once its window is over. wday < 0 = time
 *  unknown: schedules are ignored. */
modes_state_t modes_eval(modes_t *m, int wday, int min);

/** Turn a mode on or off by hand at (wday, min). On: on until turned off. Off: also
 *  skips the rest of a scheduled window it is in. */
void modes_set(modes_t *m, mode_id_t id, bool on, int wday, int min);

/** Minutes to the next schedule edge of any mode (0 = none). */
uint32_t modes_next_edge(const modes_t *m, int wday, int min);

const char *mode_name(mode_id_t id);

#ifdef __cplusplus
}
#endif
