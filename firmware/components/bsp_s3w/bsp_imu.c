// IMU: QMI8658 on the shared bus. INT1 (GPIO21, RTC-IO) on any edge — wake-on-motion
// toggles the level instead of pulsing — is deferred to the FreeRTOS timer task and
// handed to the user callback, which reads qmi8658_read_irq(). Event driven.
#include "bsp_s3w.h"

#include "bsp_s3w_pins.h"
#include "bsp_s3w_priv.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "bsp_imu";

static qmi8658_handle_t s_imu;
static bsp_imu_int_cb_t s_int_cb;
static void *s_int_ctx;

static void int_deferred(void *arg1, uint32_t arg2)
{
    (void)arg1;
    (void)arg2;
    const bsp_imu_int_cb_t cb = s_int_cb;
    if (cb) {
        cb(s_int_ctx);
    }
    bsp_wake_rearm(BSP_PIN_IMU_INT1); // for the level opposite to the new one
}

static void imu_isr(void *ctx)
{
    (void)ctx;
    bsp_wake_isr_fired(BSP_PIN_IMU_INT1);
    BaseType_t woken = pdFALSE;
    xTimerPendFunctionCallFromISR(int_deferred, NULL, 0, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

esp_err_t bsp_imu_start(void)
{
    ESP_RETURN_ON_FALSE(bsp_i2c_bus(), ESP_ERR_INVALID_STATE, TAG, "bsp_init_early first");
    if (s_imu) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(qmi8658_new(bsp_i2c_bus(), BSP_I2C_ADDR_QMI8658, BSP_I2C_FREQ_HZ, &s_imu), TAG, "qmi8658");
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << BSP_PIN_IMU_INT1,
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "int1 gpio");
    return gpio_isr_handler_add(BSP_PIN_IMU_INT1, imu_isr, NULL);
}

qmi8658_handle_t bsp_imu_handle(void)
{
    return s_imu;
}

void bsp_imu_set_int_cb(bsp_imu_int_cb_t cb, void *ctx)
{
    s_int_ctx = ctx;
    s_int_cb = cb;
}
