// QMI8658 6-axis IMU (I2C): accel/gyro, FIFO, wake-on-motion, tap, pedometer,
// self-test. Pin-agnostic: the caller owns the INT GPIO and decides when to read
// qmi8658_read_irq(). All features that raise an interrupt are routed to INT1 (the
// only IMU interrupt wired on this board).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "qmi8658_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct qmi8658_s *qmi8658_handle_t;

// STATUS1 (0x2F) activity flags
#define QMI8658_ST1_TAP      (1u << 1)
#define QMI8658_ST1_WOM      (1u << 2)
#define QMI8658_ST1_PEDO     (1u << 4)
#define QMI8658_ST1_ANY_MOT  (1u << 5)
#define QMI8658_ST1_NO_MOT   (1u << 6)
#define QMI8658_ST1_SIG_MOT  (1u << 7)
// FIFO_STATUS (0x16)
#define QMI8658_FIFO_NOT_EMPTY (1u << 4)
#define QMI8658_FIFO_OVERFLOW  (1u << 5)
#define QMI8658_FIFO_WTM       (1u << 6)
#define QMI8658_FIFO_FULL      (1u << 7)

typedef enum { QMI8658_FIFO_16 = 0, QMI8658_FIFO_32, QMI8658_FIFO_64, QMI8658_FIFO_128 } qmi8658_fifo_size_t;

typedef struct {
    uint8_t status_int; // STATUSINT (0x2D)
    uint8_t status1;    // STATUS1 (0x2F), activity flags (clear on read)
    uint8_t fifo;       // FIFO_STATUS (0x16)
} qmi8658_irq_t;

typedef enum { QMI8658_TAP_NONE = 0, QMI8658_TAP_SINGLE = 1, QMI8658_TAP_DOUBLE = 2 } qmi8658_tap_t;

esp_err_t qmi8658_new(i2c_master_bus_handle_t bus, uint8_t addr, uint32_t scl_hz, qmi8658_handle_t *out);

/** Soft reset, WHO_AM_I check, auto-increment + CTRL9 handshake set up, sensors off. */
esp_err_t qmi8658_reset(qmi8658_handle_t h);

esp_err_t qmi8658_read_id(qmi8658_handle_t h, uint8_t *who_am_i, uint8_t *revision);

esp_err_t qmi8658_config_accel(qmi8658_handle_t h, qmi8658_acc_range_t range, qmi8658_odr_t odr);
esp_err_t qmi8658_config_gyro(qmi8658_handle_t h, qmi8658_gyr_range_t range, qmi8658_odr_t odr);
/** Turn sensors on/off. Low-power accel ODRs need gyro off. Both off = lowest power. */
esp_err_t qmi8658_enable(qmi8658_handle_t h, bool accel, bool gyro);

/** Latest sample, scaled (mg, mdps). Either pointer may be NULL. */
esp_err_t qmi8658_read(qmi8658_handle_t h, int32_t acc_mg[3], int32_t gyr_mdps[3]);
esp_err_t qmi8658_read_temp(qmi8658_handle_t h, int16_t *cdeg);

/** Stream-mode FIFO of `size` frames, watermark interrupt on INT1 after `watermark` frames. */
esp_err_t qmi8658_fifo_config(qmi8658_handle_t h, qmi8658_fifo_size_t size, uint8_t watermark);
/** Drain the FIFO into buf; *len = bytes read. Frame layout: see qmi8658_fifo_parse(). */
esp_err_t qmi8658_fifo_read(qmi8658_handle_t h, uint8_t *buf, size_t cap, size_t *len);
esp_err_t qmi8658_fifo_disable(qmi8658_handle_t h);

/**
 * Wake-on-motion on INT1: accel only at a low-power ODR, threshold in mg (1..255).
 * The INT1 level toggles on every event (use any-edge interrupts).
 */
esp_err_t qmi8658_wom_enable(qmi8658_handle_t h, uint8_t threshold_mg, qmi8658_odr_t lp_odr);
esp_err_t qmi8658_wom_disable(qmi8658_handle_t h);

/** Tap/double-tap engine on INT1 (sets accel to 500 Hz ±4 g, the recommended rate). */
esp_err_t qmi8658_tap_enable(qmi8658_handle_t h);
esp_err_t qmi8658_tap_read(qmi8658_handle_t h, qmi8658_tap_t *tap, char *axis, bool *negative);

/** Built-in pedometer on INT1 (accel 62.5 Hz ±2 g; register updates every 4 steps). */
esp_err_t qmi8658_pedometer_enable(qmi8658_handle_t h);
esp_err_t qmi8658_pedometer_read(qmi8658_handle_t h, uint32_t *steps);
esp_err_t qmi8658_pedometer_reset(qmi8658_handle_t h);

/** Disable tap/pedometer/motion engines (CTRL8) and the activity interrupt. */
esp_err_t qmi8658_engines_disable(qmi8658_handle_t h);

/** Read and clear interrupt sources. */
esp_err_t qmi8658_read_irq(qmi8658_handle_t h, qmi8658_irq_t *irq);

/** Raw register access (diagnostics). */
esp_err_t qmi8658_reg_read(qmi8658_handle_t h, uint8_t reg, uint8_t *data, size_t len);
esp_err_t qmi8658_reg_write(qmi8658_handle_t h, uint8_t reg, uint8_t value);

/** Datasheet self-test. Leaves sensors off; reconfigure afterwards. */
esp_err_t qmi8658_selftest_accel(qmi8658_handle_t h, bool *pass, int32_t mg[3]);
esp_err_t qmi8658_selftest_gyro(qmi8658_handle_t h, bool *pass, int32_t dps[3]);

#ifdef __cplusplus
}
#endif
