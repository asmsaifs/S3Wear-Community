#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Accelerometer for svc_sensors (raise-to-wake, step counting). Samples are in the watch frame:
// mg, +x toward 3 o'clock, +y toward 12 o'clock, +z out of the screen (lying face
// up at rest: z = +1000). Blocking I2C: never from the UI task.

typedef struct {
    int32_t x, y, z;
} hal_accel_t;

/** An IMU was found at boot. */
bool hal_imu_present(void);

/**
 * Wake-on-motion: accelerometer in a low-power mode, the interrupt fires when the
 * acceleration changes by more than threshold_mg. Samples are not meant to be read.
 */
esp_err_t hal_imu_motion_wake(uint8_t threshold_mg);

/** Accelerometer streaming (wake-on-motion off), a new sample about every 16 ms (62.5 Hz). */
esp_err_t hal_imu_accel_start(void);

/**
 * Accelerometer on for step counting only (wake-on-motion off): low-power mode, about
 * 21 Hz. Read it through the FIFO (hal_imu_fifo_start()).
 */
esp_err_t hal_imu_accel_lp_start(void);

#define HAL_IMU_FIFO_FRAMES 128 // hal_imu_fifo_read() never returns more

/**
 * Keep every accelerometer sample in the IMU's FIFO (128 frames, no interrupt: poll it
 * with hal_imu_fifo_read() before it fills). Call after the accelerometer mode is set
 * (hal_imu_motion_wake, hal_imu_accel_start, hal_imu_accel_lp_start); resets the FIFO.
 * *period_us = the sample period of that mode.
 */
esp_err_t hal_imu_fifo_start(uint32_t *period_us);

/** Drain the FIFO: *n samples (watch frame, mg), oldest first; *overflow = some were lost. */
esp_err_t hal_imu_fifo_read(hal_accel_t *out, size_t max, size_t *n, bool *overflow);

/** Latest accelerometer sample (after hal_imu_accel_start()). */
esp_err_t hal_imu_read_accel(hal_accel_t *out);

/** Both sensors off, every engine and the FIFO off. */
esp_err_t hal_imu_off(void);

/** Read and clear the interrupt sources; *motion = a wake-on-motion event happened. */
esp_err_t hal_imu_read_irq(bool *motion);

/** IMU interrupt, from a driver task: short and non-blocking (queue only). NULL = none. */
typedef void (*hal_imu_int_cb_t)(void *ctx);
void hal_imu_set_int_cb(hal_imu_int_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
