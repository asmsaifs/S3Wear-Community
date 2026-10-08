// Sensor service (docs/03-firmware-features.md F2): owns the IMU. P3-05: raise to
// wake. P3-07: flip detection while an alarm rings. Activity, sleep and tilt come
// later (P5).
//
// One task (svc_sensors, core 0, prio 12). While the screen is off (AOD or SLEEP;
// not SAVER) and RAISE_TO_WAKE is on, the accelerometer waits in its low-power
// wake-on-motion mode. A motion interrupt opens a window in which the accelerometer
// streams and raise_detect.h looks for the raise gesture; a raise calls
// svc_power_wake(SVC_POWER_WAKE_RAISE). With the screen on the IMU is off, except
// while svc_sensors_watch_flip() is on (alarm ringing): the accelerometer then streams
// at 10 Hz and a flip face down (flip_detect.h) posts SVC_SENSORS_EVT_FLIP.
// Every call here only queues a request (except svc_sensors_suspend(), which waits).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "svc_sensors_events.h"

#ifdef __cplusplus
extern "C" {
#endif

/** After svc_power and the settings. Without an IMU it starts but does nothing. */
esp_err_t svc_sensors_start(void);

/**
 * Diagnostics take the IMU (console `imu`, factory test): true turns it off and
 * stops using it, false gives it back (the IMU is reconfigured). Waits until done.
 */
esp_err_t svc_sensors_suspend(bool suspend);

/** Watch for a flip face down (counted; svc_alarm while ringing). Queued. */
esp_err_t svc_sensors_watch_flip(bool on);

/** Log every raise window's result (tuning, console `sensors trace`). */
void svc_sensors_set_trace(bool on);

typedef struct {
    int32_t x, y, z; // mg, watch frame (hal_imu.h)
} svc_sensors_accel_t;

/** One accelerometer sample now (turns the accelerometer on briefly if needed). Waits. */
esp_err_t svc_sensors_read_accel(svc_sensors_accel_t *out);

typedef enum {
    SVC_SENSORS_MODE_OFF = 0, // IMU off (screen on, raise off, saver, no IMU)
    SVC_SENSORS_MODE_WAIT,    // wake-on-motion armed
    SVC_SENSORS_MODE_WINDOW,  // looking for a raise
    SVC_SENSORS_MODE_SUSPENDED,
    SVC_SENSORS_MODE_FLIP,    // watching for a flip (svc_sensors_watch_flip)
} svc_sensors_mode_t;

typedef struct {
    svc_sensors_mode_t mode;
    bool present;
    bool raise_enabled; // RAISE_TO_WAKE, and not in sleep or theater mode
    uint8_t cone_deg;   // from RAISE_SENSITIVITY
    uint32_t motion_irqs;  // wake-on-motion interrupts
    uint32_t windows;      // raise windows opened
    uint32_t raises;       // raises detected
    uint32_t samples;      // accelerometer samples read in windows
    uint32_t errors;       // IMU I2C errors
    uint32_t flips;        // flips detected
    // Last window
    int16_t last_min_deg;  // largest angle from screen-up (0 = face up)
    int16_t last_end_deg;  // angle at the end
    uint16_t last_ms;
    bool last_raise;
    uint32_t stack_free;   // task stack high-water mark, bytes
} svc_sensors_stats_t;

void svc_sensors_get_stats(svc_sensors_stats_t *out);
const char *svc_sensors_mode_name(svc_sensors_mode_t mode);

#ifdef __cplusplus
}
#endif
