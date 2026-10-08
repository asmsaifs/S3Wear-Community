// Alarm scheduler (docs/03-firmware-features.md F6). Pure logic, no ESP-IDF: built by
// firmware/host_test and the simulator.
//
// An alarm is a local wall-clock time (hour:minute) with repeat days, so it follows
// the time zone and DST: the next occurrence is found in local time (tz_posix.h) and
// converted to UTC. A local time that does not exist (spring-forward gap) rings at the
// same distance after the gap (02:30 -> 03:30); one that happens twice (fall back)
// rings at the first.
//
// The set remembers how far it has rung (`checked`): an occurrence in
// (checked, now] is due. Occurrences more than ALARM_CATCHUP_S in the past are
// skipped, so a slow boot after a deep-sleep wake still rings but a watch that was
// off for a day does not ring for yesterday.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tz_posix.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ALARM_MAX           16
#define ALARM_LABEL_MAX     23
#define ALARM_SNOOZE_MIN    5   // snooze length range, minutes
#define ALARM_SNOOZE_MAX    15
#define ALARM_SNOOZE_DEFAULT 10
#define ALARM_CATCHUP_S     (5 * 60)
#define ALARM_NEVER         INT64_MAX

/** days bits: bit d = weekday d (0 = Sunday .. 6 = Saturday). 0 = once. */
#define ALARM_DAYS_WEEKDAYS 0x3Eu
#define ALARM_DAYS_WEEKEND  0x41u
#define ALARM_DAYS_ALL      0x7Fu

typedef struct {
    uint8_t id;         // 1..255, stable; 0 = new (alarm_set_put assigns one)
    bool enabled;
    uint8_t hour;       // local, 0..23
    uint8_t minute;     // 0..59
    uint8_t days;       // repeat days, 0 = once (disabled after it rings)
    uint8_t snooze_min; // ALARM_SNOOZE_MIN..ALARM_SNOOZE_MAX
    char label[ALARM_LABEL_MAX + 1]; // "" = "Alarm"
} alarm_t;

typedef struct {
    alarm_t items[ALARM_MAX]; // sorted by time of day, then id
    uint8_t count;
    uint8_t last_id;          // ids are handed out after this one
    uint8_t snooze_id;        // alarm being snoozed, 0 = none
    int64_t snooze_at;        // UTC s
    int64_t checked;          // UTC s: occurrences at or before this have rung (or were skipped)
} alarm_set_t;

/** What is due now. */
typedef struct {
    uint8_t id;      // alarm
    bool snooze;     // a snooze ending, not the alarm's own time
    int64_t at;      // UTC s of the occurrence
} alarm_due_t;

void alarm_set_init(alarm_set_t *set);

/** Range check of one alarm (id is not checked). */
bool alarm_valid(const alarm_t *a);

/** A new alarm with the defaults: enabled, once, 10 min snooze. */
void alarm_default(alarm_t *a, uint8_t hour, uint8_t minute);

/** Add (a->id == 0) or replace (same id) an alarm. Returns its id, 0 if invalid,
 *  full or the id is unknown. Changing an alarm cancels its snooze. */
uint8_t alarm_set_put(alarm_set_t *set, const alarm_t *a);
/** alarm_set_put() by the user at now: an occurrence that already passed (an alarm set
 *  for the current minute) does not ring at once. If something was already due, it
 *  still rings (checked stays). */
uint8_t alarm_set_put_at(alarm_set_t *set, const alarm_t *a, const tz_posix_t *tz, int64_t now);
bool alarm_set_remove(alarm_set_t *set, uint8_t id);
alarm_t *alarm_set_find(alarm_set_t *set, uint8_t id);
const alarm_t *alarm_set_find_const(const alarm_set_t *set, uint8_t id);

/** UTC instant of local y-m-d h:mi in tz (see the gap/overlap rule above). */
int64_t alarm_local_to_utc(const tz_posix_t *tz, int year, int month, int mday, int hour, int minute);

/** First occurrence of a strictly after `after` (UTC s); ALARM_NEVER if disabled. */
int64_t alarm_next(const alarm_t *a, const tz_posix_t *tz, int64_t after);

/** Earliest alarm or snooze strictly after `after`; ALARM_NEVER if none. out may be NULL. */
int64_t alarm_set_next(const alarm_set_t *set, const tz_posix_t *tz, int64_t after, alarm_due_t *out);

/** True if an occurrence is due at now (in (max(checked, now - CATCHUP), now]). */
bool alarm_set_due(const alarm_set_t *set, const tz_posix_t *tz, int64_t now, alarm_due_t *out);

/** Everything due at now has rung: one-time alarms among it are disabled, a due
 *  snooze is cleared, checked = now. */
void alarm_set_mark_rung(alarm_set_t *set, const tz_posix_t *tz, int64_t now);

/** Ring alarm id again at now + its snooze length. */
void alarm_set_snooze(alarm_set_t *set, uint8_t id, int64_t now);
void alarm_set_cancel_snooze(alarm_set_t *set);


/** The clock was set: if it went back past checked, start checking from now again. */
void alarm_set_clock_changed(alarm_set_t *set, int64_t now);

/** "Mon-Fri", "Every day", "Weekends", "Once", "Mon Wed Fri" into buf (>= 32 bytes). */
void alarm_days_text(uint8_t days, char *buf, size_t len);

// --- Persistence (svc_alarm keeps one NVS blob) -------------------------------------

#define ALARM_BLOB_MAX (24 + ALARM_MAX * (6 + ALARM_LABEL_MAX + 1) + 4)

/** Little-endian blob with a version and CRC-32. Returns its length, 0 if len is too small. */
size_t alarm_set_encode(const alarm_set_t *set, uint8_t *buf, size_t len);
/** false (set untouched) on a bad version, CRC, length or any invalid alarm. */
bool alarm_set_decode(alarm_set_t *set, const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif
