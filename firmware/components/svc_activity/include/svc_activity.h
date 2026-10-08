// Activity service (docs/03 F12, docs/02 §7 "Activity", P5-01): steps from the software
// step detector (step_detect.h) on every accelerometer sample, which svc_sensors hands over
// in FIFO batches; distance, kcal, active minutes and goals for the local day
// (activity_day.h), from the Health profile settings (HEIGHT_CM, WEIGHT_KG, SEX,
// BIRTH_YEAR, STEP_GOAL, ACTIVE_GOAL_MIN).
//
// No task of its own: batches are processed on the svc_sensors task (~every 4.6 s), the
// day's totals are saved to NVS through svc_worker (at most every 10 min while they change,
// at the end of a day, and when the watch powers off). The per-minute buckets live only
// in RAM until health_db (P5-02).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "activity_day.h"
#include "esp_err.h"
#include "svc_activity_events.h"

#ifdef __cplusplus
extern "C" {
#endif

/** After svc_settings, svc_time, svc_worker and svc_sensors. Restores today's totals. */
esp_err_t svc_activity_start(void);

typedef struct {
    activity_summary_t today; // kcal includes the BMR up to now
    uint16_t cadence_spm;     // while walking, else 0
    bool walking;
} svc_activity_state_t;

/** Today's totals now (also rolls the day over at midnight without new steps). */
void svc_activity_get(svc_activity_state_t *out);

/** Today's steps per local minute: n ≤ ACT_MINUTES entries from minute 0. */
void svc_activity_get_minutes(uint16_t *out, size_t n);

/** Start today from zero (console). Saved. */
esp_err_t svc_activity_reset_today(void);

/**
 * Record every accelerometer sample the step detector sees to a CSV file (`imu log`
 * format, firmware/test/activity/README.md) for `seconds` (0 = until stopped). Files go
 * through svc_worker. ESP_ERR_INVALID_STATE if a log is running.
 */
esp_err_t svc_activity_log_start(const char *path, uint32_t seconds);
esp_err_t svc_activity_log_stop(void);

typedef struct {
    uint32_t batches;
    uint32_t samples;
    uint32_t restarts;     // batches after a FIFO gap
    uint32_t detector_steps; // counted by the detector since boot
    uint32_t saves;
    uint32_t save_errors;
    bool logging;
    uint32_t log_samples;
    uint32_t log_dropped;  // samples not written (worker queue full, write error)
    char log_path[48];
} svc_activity_stats_t;

void svc_activity_get_stats(svc_activity_stats_t *out);

#ifdef __cplusplus
}
#endif
