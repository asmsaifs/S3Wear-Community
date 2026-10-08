#include "hal_imu.h"

#include "bsp_s3w.h"
#include "esp_check.h"

static const char *TAG = "hal_imu";

#define WOM_ODR QMI8658_ODR_LP_21HZ   // low-power accel while waiting for motion
#define STEPS_ODR QMI8658_ODR_LP_21HZ // step counting only (measured ~23 Hz); same rate as wake-on-motion
#define WOM_RANGE QMI8658_ACC_8G      // what qmi8658_wom_enable() sets
#define STREAM_RANGE QMI8658_ACC_4G

static const int k_map[3] = BSP_IMU_AXIS_MAP;
static const int k_sign[3] = BSP_IMU_AXIS_SIGN;

// Accelerometer mode as last set here, for FIFO frames (raw values, no range attached).
static qmi8658_acc_range_t s_range = STREAM_RANGE;
static qmi8658_odr_t s_odr = QMI8658_ODR_62_5HZ;
static uint8_t s_fifo_buf[HAL_IMU_FIFO_FRAMES * 6]; // svc_sensors task only

static void to_watch(const int32_t a[3], hal_accel_t *out)
{
    out->x = k_sign[0] * a[k_map[0]];
    out->y = k_sign[1] * a[k_map[1]];
    out->z = k_sign[2] * a[k_map[2]];
}

bool hal_imu_present(void)
{
    return bsp_imu_handle() != NULL;
}

esp_err_t hal_imu_motion_wake(uint8_t threshold_mg)
{
    qmi8658_handle_t h = bsp_imu_handle();
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_STATE, TAG, "no IMU");
    s_range = WOM_RANGE;
    s_odr = WOM_ODR;
    return qmi8658_wom_enable(h, threshold_mg, WOM_ODR);
}

esp_err_t hal_imu_accel_start(void)
{
    qmi8658_handle_t h = bsp_imu_handle();
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_STATE, TAG, "no IMU");
    ESP_RETURN_ON_ERROR(qmi8658_wom_disable(h), TAG, "wom off");
    s_range = STREAM_RANGE;
    s_odr = QMI8658_ODR_62_5HZ;
    ESP_RETURN_ON_ERROR(qmi8658_config_accel(h, s_range, s_odr), TAG, "accel");
    return qmi8658_enable(h, true, false);
}

esp_err_t hal_imu_accel_lp_start(void)
{
    qmi8658_handle_t h = bsp_imu_handle();
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_STATE, TAG, "no IMU");
    ESP_RETURN_ON_ERROR(qmi8658_wom_disable(h), TAG, "wom off");
    s_range = STREAM_RANGE;
    s_odr = STEPS_ODR;
    ESP_RETURN_ON_ERROR(qmi8658_config_accel(h, s_range, s_odr), TAG, "accel");
    return qmi8658_enable(h, true, false);
}

esp_err_t hal_imu_fifo_start(uint32_t *period_us)
{
    qmi8658_handle_t h = bsp_imu_handle();
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_STATE, TAG, "no IMU");
    const uint32_t mhz = qmi8658_odr_mhz(s_odr);
    ESP_RETURN_ON_FALSE(mhz, ESP_ERR_INVALID_STATE, TAG, "odr");
    *period_us = (uint32_t)(1000000000ull / mhz);
    // The watermark is unused (polled), any value in range.
    return qmi8658_fifo_config(h, QMI8658_FIFO_128, HAL_IMU_FIFO_FRAMES / 2, false);
}

esp_err_t hal_imu_fifo_read(hal_accel_t *out, size_t max, size_t *n, bool *overflow)
{
    qmi8658_handle_t h = bsp_imu_handle();
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_STATE, TAG, "no IMU");
    *n = 0;
    size_t len = 0;
    uint8_t status = 0;
    const size_t cap = (max < HAL_IMU_FIFO_FRAMES ? max : HAL_IMU_FIFO_FRAMES) * 6;
    ESP_RETURN_ON_ERROR(qmi8658_fifo_read(h, s_fifo_buf, cap, &len, &status), TAG, "fifo");
    *overflow = (status & (QMI8658_FIFO_OVERFLOW | QMI8658_FIFO_FULL)) != 0;
    for (size_t off = 0; off + 6 <= len; off += 6) {
        const qmi8658_raw3_t r = qmi8658_unpack3(&s_fifo_buf[off]);
        const int32_t a[3] = {qmi8658_acc_mg(r.x, s_range), qmi8658_acc_mg(r.y, s_range),
                              qmi8658_acc_mg(r.z, s_range)};
        to_watch(a, &out[(*n)++]);
    }
    return ESP_OK;
}

esp_err_t hal_imu_read_accel(hal_accel_t *out)
{
    qmi8658_handle_t h = bsp_imu_handle();
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_STATE, TAG, "no IMU");
    int32_t a[3];
    ESP_RETURN_ON_ERROR(qmi8658_read(h, a, NULL), TAG, "read");
    to_watch(a, out);
    return ESP_OK;
}

esp_err_t hal_imu_off(void)
{
    qmi8658_handle_t h = bsp_imu_handle();
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_STATE, TAG, "no IMU");
    ESP_RETURN_ON_ERROR(qmi8658_wom_disable(h), TAG, "wom off"); // also turns both sensors off
    ESP_RETURN_ON_ERROR(qmi8658_fifo_disable(h), TAG, "fifo off");
    return qmi8658_engines_disable(h);
}

esp_err_t hal_imu_read_irq(bool *motion)
{
    qmi8658_handle_t h = bsp_imu_handle();
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_STATE, TAG, "no IMU");
    qmi8658_irq_t irq;
    ESP_RETURN_ON_ERROR(qmi8658_read_irq(h, &irq), TAG, "irq");
    *motion = (irq.status1 & QMI8658_ST1_WOM) != 0;
    return ESP_OK;
}

void hal_imu_set_int_cb(hal_imu_int_cb_t cb, void *ctx)
{
    bsp_imu_set_int_cb(cb, ctx); // same signature
}
