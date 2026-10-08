// Time service (docs/03 F1, docs/02 §5): system time, time zone, 12/24 h, the
// "time valid" flag, RTC drift calibration and system-clock discipline.
//
// - Boot: RTC (UTC) -> system time; an RTC that lost power means "time unknown" until
//   a sync or a manual set.
// - Zone: a POSIX TZ string (setting TIMEZONE). svc_time computes DST itself
//   (tz_posix.h) and gives newlib only the offset in force, re-applied at each
//   transition, so localtime_r()/strftime() are right everywhere.
// - Sync: svc_time_set_utc_ms() with a reference source (phone, SNTP) also measures
//   the RTC drift and trims the PCF85063 offset register (time_drift.h).
// - Discipline: system time runs on the RC slow clock in light sleep; it is checked
//   against the RTC every 10 min (when awake) and on screen-on, and stepped if off.
//
// No task: work runs in the caller, the bus task (settings/power events) or esp_timer
// callbacks; each does at most one short I2C transfer.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"
#include "svc_time_events.h"
#include "time_drift.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SVC_TIME_SRC_NONE = 0, // time unknown
    SVC_TIME_SRC_RTC,      // from the PCF85063 at boot
    SVC_TIME_SRC_PHONE,    // TimeSync from the companion (reference: calibrates drift)
    SVC_TIME_SRC_SNTP,     // network time (reference: calibrates drift)
    SVC_TIME_SRC_MANUAL,   // set by the user or the console (not a reference)
} svc_time_source_t;

/**
 * Boot step 4, after svc_settings_init() and bsp_rtc_start(): drift state from NVS,
 * offset register, RTC -> system time, zone from settings, timers, subscriptions.
 */
esp_err_t svc_time_start(void);

/** False while the time is unknown (UI shows "--:--"). */
bool svc_time_is_valid(void);
svc_time_source_t svc_time_source(void);
const char *svc_time_source_name(svc_time_source_t src);

/**
 * Set the time (Unix ms, UTC; 2000..2099). Reference sources (PHONE, SNTP) feed the
 * drift calibration; MANUAL restarts its window. System time is set now; the RTC is
 * rewritten on the next whole second (esp_timer). Does an I2C read: not from the UI task.
 */
esp_err_t svc_time_set_utc_ms(int64_t utc_ms, svc_time_source_t src);

/** Validate a POSIX TZ string and store it (setting TIMEZONE); applied via the settings event. */
esp_err_t svc_time_set_tz(const char *posix_tz);

bool svc_time_is_24h(void);
esp_err_t svc_time_set_24h(bool h24);

/** Local time in the current zone (tz_posix; tm_isdst set). Safe from any task. */
void svc_time_localtime(time_t utc, struct tm *out);

/** Current local - UTC offset in seconds. */
int32_t svc_time_utc_offset(void);

typedef struct {
    bool valid;
    svc_time_source_t source;
    char tz[64];          // POSIX TZ in use
    char newlib_tz[24];   // fixed-offset string handed to newlib now
    char abbr[16];        // zone abbreviation now (CET, CEST, +06)
    int32_t offset_s;     // local - UTC now
    bool dst;
    int64_t next_transition; // Unix s, INT64_MAX if none
    time_drift_t drift;
    bool rtc_write_pending;
} svc_time_status_t;

void svc_time_get_status(svc_time_status_t *out);

/** Check the system clock against the RTC now (console); returns the correction applied in ms. */
int64_t svc_time_discipline_now(void);

/** Forget the drift calibration (steps back to 0, window restarted, offset register cleared). */
esp_err_t svc_time_drift_reset(void);

#ifdef __cplusplus
}
#endif
