// QMI8658 data conversions (pure logic, no ESP-IDF: host-tested).
// Register map: QMI8658A datasheet, cross-checked with SensorLib (docs/vendor/README.md).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { QMI8658_ACC_2G = 0, QMI8658_ACC_4G, QMI8658_ACC_8G, QMI8658_ACC_16G } qmi8658_acc_range_t;

typedef enum {
    QMI8658_GYR_16DPS = 0,
    QMI8658_GYR_32DPS,
    QMI8658_GYR_64DPS,
    QMI8658_GYR_128DPS,
    QMI8658_GYR_256DPS,
    QMI8658_GYR_512DPS,
    QMI8658_GYR_1024DPS,
    QMI8658_GYR_2048DPS,
} qmi8658_gyr_range_t;

// ODR codes (CTRL2/CTRL3 [3:0]). Accelerometer-only rates; with the gyro on the
// rate follows the gyro clock (e.g. 62.5 Hz becomes 56.05 Hz).
typedef enum {
    QMI8658_ODR_1000HZ = 3,
    QMI8658_ODR_500HZ = 4,
    QMI8658_ODR_250HZ = 5,
    QMI8658_ODR_125HZ = 6,
    QMI8658_ODR_62_5HZ = 7,
    QMI8658_ODR_31_25HZ = 8,
    QMI8658_ODR_LP_128HZ = 12, // accelerometer low-power modes, gyro must be off
    QMI8658_ODR_LP_21HZ = 13,
    QMI8658_ODR_LP_11HZ = 14,
    QMI8658_ODR_LP_3HZ = 15,
} qmi8658_odr_t;

typedef struct {
    int16_t x, y, z;
} qmi8658_raw3_t;

/** Raw accelerometer LSB -> milli-g for a range (full scale = ±range over ±32768). */
int32_t qmi8658_acc_mg(int16_t raw, qmi8658_acc_range_t range);

/** Raw gyroscope LSB -> milli-degrees per second. */
int32_t qmi8658_gyr_mdps(int16_t raw, qmi8658_gyr_range_t range);

/** Nominal ODR in milli-Hz (accelerometer-only operation), 0 for unknown codes. */
uint32_t qmi8658_odr_mhz(qmi8658_odr_t odr);

/** Little-endian 3-axis sample from 6 bytes. */
qmi8658_raw3_t qmi8658_unpack3(const uint8_t *p);

/** Temperature registers (LSB = 1/256 °C) -> centi-°C. */
int16_t qmi8658_temp_cdeg(uint8_t lo, uint8_t hi);

/** FIFO byte count from FIFO_SMPL_CNT (0x15) and FIFO_STATUS[1:0] (0x16): 2 * (msb:lsb). */
size_t qmi8658_fifo_bytes(uint8_t cnt_lsb, uint8_t status);

/**
 * Split raw FIFO bytes into frames. Frame = accel (6 B) then gyro (6 B) for each
 * enabled sensor. Returns the number of complete frames written to acc/gyr (either
 * may be NULL if that sensor is off).
 */
size_t qmi8658_fifo_parse(const uint8_t *data, size_t len, bool acc_on, bool gyr_on, qmi8658_raw3_t *acc,
                          qmi8658_raw3_t *gyr, size_t max_frames);

/** Tap thresholds are squared accelerations in units of 0.001 g^2. */
uint16_t qmi8658_g2_units(float g_squared);

/** Unsigned fixed point with 7 fraction bits (tap alpha/gamma). */
uint8_t qmi8658_u0_7(float ratio);

/** Self-test results: accel dV in U5.11 (1/2048 g), pass if |x|,|y|,|z| > 200 mg. */
bool qmi8658_acc_selftest_pass(const qmi8658_raw3_t *dv, int32_t mg_out[3]);

/** Gyro dV in U12.4 (1/16 dps), pass if |x|,|y|,|z| > 300 dps. */
bool qmi8658_gyr_selftest_pass(const qmi8658_raw3_t *dv, int32_t dps_out[3]);

#ifdef __cplusplus
}
#endif
