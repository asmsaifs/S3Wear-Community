#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Accelerometer for svc_sensors (raise-to-wake). Samples are in the watch frame:
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

/** Latest accelerometer sample (after hal_imu_accel_start()). */
esp_err_t hal_imu_read_accel(hal_accel_t *out);

/** Both sensors off, every engine off. */
esp_err_t hal_imu_off(void);

/** Read and clear the interrupt sources; *motion = a wake-on-motion event happened. */
esp_err_t hal_imu_read_irq(bool *motion);

/** IMU interrupt, from a driver task: short and non-blocking (queue only). NULL = none. */
typedef void (*hal_imu_int_cb_t)(void *ctx);
void hal_imu_set_int_cb(hal_imu_int_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
