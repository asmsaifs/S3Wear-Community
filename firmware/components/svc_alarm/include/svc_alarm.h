// Alarm service (docs/03-firmware-features.md F6, docs/02 §7): alarms, snooze,
// countdown timers and ringing.
//
// - Alarms (alarm_sched.h) are kept in one NVS blob and survive a reboot. The next
//   alarm is programmed in an esp_timer (wakes light sleep, sub-ms) and in the
//   PCF85063 alarm (backup interrupt, also a light-sleep wake source); for WATCH-ONLY
//   deep sleep it is handed to svc_power, which arms the ESP RTC timer a little early.
// - Countdown timers (timer_set.h) run on the monotonic clock in a second esp_timer.
//   They are not persisted.
// - Ringing: the screen is woken (svc_power), SVC_ALARM_EVT_RING tells the UI to show
//   the ring screen, a beep pattern plays with rising volume (alarms bypass DND and
//   Silent; ALARM volume), and the IMU watches for a flip (svc_sensors), which
//   snoozes. Unanswered, an alarm snoozes itself after SVC_ALARM_RING_MS, up to
//   SVC_ALARM_AUTO_SNOOZES times; a timer stops.
//
// One task (svc_alarm, core 0, prio 13), idle on its queue except while ringing, when
// it feeds the speaker. The API may be called from any task, including the UI task:
// it changes RAM under a mutex and queues the slow part (NVS, RTC, audio).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "alarm_sched.h"
#include "esp_err.h"
#include "svc_alarm_events.h"
#include "timer_set.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SVC_ALARM_RING_MS     (2 * 60 * 1000)
#define SVC_ALARM_AUTO_SNOOZES 3
#define SVC_TIMER_RING_MS     (60 * 1000)

/** After svc_time, svc_power and svc_sensors: loads the alarms, rings anything missed
 *  in the last ALARM_CATCHUP_S (e.g. a deep-sleep wake) and schedules the next. */
esp_err_t svc_alarm_start(void);

// --- Alarms --------------------------------------------------------------------------

/** Copy of the alarms (sorted by time of day) and snooze state. */
void svc_alarm_get(alarm_set_t *out);
/** Add (id 0) or replace an alarm; the id in *id (may be NULL).
 *  ESP_ERR_NO_MEM when ALARM_MAX exist, ESP_ERR_INVALID_ARG if invalid or unknown. */
esp_err_t svc_alarm_put(const alarm_t *a, uint8_t *id);
esp_err_t svc_alarm_delete(uint8_t id);
/** Next alarm or snooze (UTC s), 0 if none. */
int64_t svc_alarm_next(void);

// --- Timers ----------------------------------------------------------------------------

/** Copy of the timers and the clock they run on (ms, for timer_left_ms()). */
void svc_alarm_timers(timer_set_t *out, uint32_t *now_ms);
esp_err_t svc_alarm_timer_start(uint32_t duration_ms, uint8_t *id);

typedef enum {
    SVC_TIMER_PAUSE,
    SVC_TIMER_RESUME,
    SVC_TIMER_RESTART,
    SVC_TIMER_REMOVE,
} svc_timer_action_t;

esp_err_t svc_alarm_timer_action(uint8_t id, svc_timer_action_t action);

// --- Ringing ----------------------------------------------------------------------------

/** Snooze the ringing alarm (a ringing timer is stopped). */
esp_err_t svc_alarm_snooze(void);
/** Stop the ringing alarm or timer (a timer is removed). */
esp_err_t svc_alarm_dismiss(void);
/** True while ringing; *out (may be NULL) gets what rings. */
bool svc_alarm_ringing(svc_alarm_evt_ring_t *out);

typedef struct {
    uint32_t rings;          // since boot
    int32_t last_late_ms;    // last alarm: ring start minus its time (negative = early)
    uint32_t rtc_irqs;       // PCF85063 alarm interrupts
    int64_t next_alarm;      // UTC s, 0 = none
    bool rtc_armed;          // PCF85063 alarm programmed
    bool audio;              // speaker available for ringing
    uint32_t stack_free;     // task stack high-water mark, bytes
} svc_alarm_stats_t;

void svc_alarm_get_stats(svc_alarm_stats_t *out);

#ifdef __cplusplus
}
#endif
