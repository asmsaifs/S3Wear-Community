// RTC: PCF85063 on the shared bus. The alarm INT (GPIO39, falling edge) is
// deferred to the FreeRTOS timer task, which clears AF over I2C and then calls the
// user callback in task context. Event driven: no polling.
#include "bsp_s3w.h"

#include "bsp_s3w_pins.h"
#include "bsp_s3w_priv.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "bsp_rtc";

static pcf85063_handle_t s_rtc;
static bsp_rtc_alarm_cb_t s_alarm_cb;
static void *s_alarm_ctx;

static void alarm_deferred(void *arg1, uint32_t arg2)
{
    (void)arg1;
    (void)arg2;
    bool fired = false;
    if (pcf85063_check_alarm(s_rtc, &fired) == ESP_OK && fired && s_alarm_cb) {
        s_alarm_cb(s_alarm_ctx);
    }
    bsp_wake_rearm(BSP_PIN_RTC_INT); // AF is clear, INT released
}

static void rtc_isr(void *ctx)
{
    (void)ctx;
    bsp_wake_isr_fired(BSP_PIN_RTC_INT);
    BaseType_t woken = pdFALSE;
    xTimerPendFunctionCallFromISR(alarm_deferred, NULL, 0, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

esp_err_t bsp_rtc_start(void)
{
    ESP_RETURN_ON_FALSE(bsp_i2c_bus(), ESP_ERR_INVALID_STATE, TAG, "bsp_init_early first");
    if (s_rtc) {
        return ESP_OK;
    }
    const pcf85063_config_t cfg = {
        .bus = bsp_i2c_bus(),
        .addr = BSP_I2C_ADDR_PCF85063,
        .scl_hz = BSP_I2C_FREQ_HZ,
        .int_gpio = BSP_PIN_RTC_INT,
        .isr_cb = rtc_isr,
    };
    return pcf85063_new(&cfg, &s_rtc);
}

pcf85063_handle_t bsp_rtc_handle(void)
{
    return s_rtc;
}

void bsp_rtc_set_alarm_cb(bsp_rtc_alarm_cb_t cb, void *ctx)
{
    s_alarm_ctx = ctx;
    s_alarm_cb = cb;
}
