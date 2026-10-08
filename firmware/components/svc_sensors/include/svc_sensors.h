// Sensor service (docs/03-firmware-features.md F2): owns the IMU. P3-05: raise to
// wake. P3-07: flip detection while an alarm rings. P8-04: the accelerometer streamed to
// a mini app. P5-01: every accelerometer sample, through the IMU's FIFO, to svc_activity.
//
// One task (svc_sensors, core 0, prio 12). While the screen is off (AOD or SLEEP;
// not SAVER) and RAISE_TO_WAKE is on, the accelerometer waits in its low-power
// wake-on-motion mode. A motion interrupt opens a window in which the accelerometer
// streams and raise_detect.h looks for the raise gesture; a raise calls
// svc_power_wake(SVC_POWER_WAKE_RAISE). With the screen on the IMU is off, except
// while svc_sensors_watch_flip() is on (alarm ringing): the accelerometer then streams
// at 10 Hz and a flip face down (flip_detect.h) posts SVC_SENSORS_EVT_FLIP. While a mini app
// streams (svc_sensors_stream) and the screen is on, the accelerometer streams to it; a ringing
// alarm's flip watch takes precedence.
// Batches (P5-01): while a client is set (svc_sensors_set_batch_cb), the accelerometer never
// stops (mode STEPS instead of OFF: low-power 21 Hz) and its FIFO keeps every sample of every
// mode (except suspended); the task drains it when 96 of its 128 frames are due (~4.6 s at
// 21 Hz, ~1.5 s at 62.5 Hz) and before each reconfiguration, and hands the samples over.
// The FIFO stays empty in wake-on-motion (measured), so steps are lost from the first
// movement until a raise window opens (~0.2 s); a window that ends at its 3 s limit with the
// wrist still moving is followed at once by the next one instead of wake-on-motion.
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

/** An accelerometer sample for the app stream, on the svc_sensors task: copy it and return. */
typedef void (*svc_sensors_sample_cb_t)(void *ctx, const svc_sensors_accel_t *a);

/**
 * Streams the accelerometer to cb about rate_hz times a second (1..62; the sensor runs at
 * 62.5 Hz) while the screen is on. rate_hz 0 stops. One client (the mini app runtime). Queued.
 */
esp_err_t svc_sensors_stream(uint16_t rate_hz, svc_sensors_sample_cb_t cb, void *ctx);

/**
 * Accelerometer samples from the FIFO, oldest first, nominally period_us apart (the IMU's
 * clock: the low-power rate runs ~10 % fast); the last one is from just before end_ms
 * (esp_timer ms, when the FIFO was read). restart: the FIFO was restarted or overflowed since the
 * previous batch (a gap; the rate may have changed).
 */
typedef struct {
    const svc_sensors_accel_t *s;
    uint16_t n;
    bool restart;
    uint32_t period_us;
    uint32_t end_ms;
} svc_sensors_batch_t;

/** On the svc_sensors task: process or copy the samples and return. */
typedef void (*svc_sensors_batch_cb_t)(void *ctx, const svc_sensors_batch_t *b);

/** The batch client (svc_activity); NULL stops. Queued. */
esp_err_t svc_sensors_set_batch_cb(svc_sensors_batch_cb_t cb, void *ctx);

typedef enum {
    SVC_SENSORS_MODE_OFF = 0, // IMU off (screen on, raise off, saver, no IMU)
    SVC_SENSORS_MODE_WAIT,    // wake-on-motion armed
    SVC_SENSORS_MODE_WINDOW,  // looking for a raise
    SVC_SENSORS_MODE_SUSPENDED,
    SVC_SENSORS_MODE_FLIP,    // watching for a flip (svc_sensors_watch_flip)
    SVC_SENSORS_MODE_STREAM,  // streaming to a mini app (svc_sensors_stream)
    SVC_SENSORS_MODE_STEPS,   // only the FIFO for the batch client (low-power 21 Hz)
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
    uint32_t streamed;     // samples streamed to the app
    uint32_t fifo_samples; // samples handed to the batch client
    uint32_t fifo_reads;   // FIFO drains with samples
    uint32_t fifo_restarts; // FIFO (re)starts
    uint32_t fifo_overflows; // drains that found samples lost
    uint32_t fifo_period_us; // sample period now, 0 = FIFO off
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
