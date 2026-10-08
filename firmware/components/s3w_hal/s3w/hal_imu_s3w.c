#include "hal_imu.h"

#include "bsp_s3w.h"
#include "esp_check.h"

static const char *TAG = "hal_imu";

#define WOM_ODR QMI8658_ODR_LP_21HZ // low-power accel while waiting for motion

static const int k_map[3] = BSP_IMU_AXIS_MAP;
static const int k_sign[3] = BSP_IMU_AXIS_SIGN;

bool hal_imu_present(void)
{
    return bsp_imu_handle() != NULL;
}

esp_err_t hal_imu_motion_wake(uint8_t threshold_mg)
{
    qmi8658_handle_t h = bsp_imu_handle();
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_STATE, TAG, "no IMU");
    return qmi8658_wom_enable(h, threshold_mg, WOM_ODR);
}

esp_err_t hal_imu_accel_start(void)
{
    qmi8658_handle_t h = bsp_imu_handle();
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_STATE, TAG, "no IMU");
    ESP_RETURN_ON_ERROR(qmi8658_wom_disable(h), TAG, "wom off");
    ESP_RETURN_ON_ERROR(qmi8658_config_accel(h, QMI8658_ACC_4G, QMI8658_ODR_62_5HZ), TAG, "accel");
    return qmi8658_enable(h, true, false);
}

esp_err_t hal_imu_read_accel(hal_accel_t *out)
{
    qmi8658_handle_t h = bsp_imu_handle();
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_STATE, TAG, "no IMU");
    int32_t a[3];
    ESP_RETURN_ON_ERROR(qmi8658_read(h, a, NULL), TAG, "read");
    out->x = k_sign[0] * a[k_map[0]];
    out->y = k_sign[1] * a[k_map[1]];
    out->z = k_sign[2] * a[k_map[2]];
    return ESP_OK;
}

esp_err_t hal_imu_off(void)
{
    qmi8658_handle_t h = bsp_imu_handle();
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_STATE, TAG, "no IMU");
    ESP_RETURN_ON_ERROR(qmi8658_wom_disable(h), TAG, "wom off"); // also turns both sensors off
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
