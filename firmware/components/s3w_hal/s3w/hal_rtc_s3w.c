#include "hal_rtc.h"

#include "bsp_s3w.h"
#include "esp_check.h"

static const char *TAG = "hal_rtc";

static hal_rtc_alarm_cb_t s_cb;
static void *s_ctx;

esp_err_t hal_rtc_get(time_t *utc, bool *valid)
{
    pcf85063_handle_t h = bsp_rtc_handle();
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_STATE, TAG, "RTC not started");
    struct tm tm;
    bool osc_stopped = true;
    ESP_RETURN_ON_ERROR(pcf85063_get_time(h, &tm, &osc_stopped), TAG, "read");
    *utc = (time_t)pcf85063_tm_to_unix(&tm);
    *valid = !osc_stopped;
    return ESP_OK;
}

esp_err_t hal_rtc_set(time_t utc)
{
    pcf85063_handle_t h = bsp_rtc_handle();
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_STATE, TAG, "RTC not started");
    struct tm tm;
    gmtime_r(&utc, &tm);
    return pcf85063_set_time(h, &tm);
}

esp_err_t hal_rtc_set_offset(int8_t steps)
{
    pcf85063_handle_t h = bsp_rtc_handle();
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_STATE, TAG, "RTC not started");
    return pcf85063_set_offset(h, steps, false);
}

esp_err_t hal_rtc_set_alarm(time_t utc)
{
    pcf85063_handle_t h = bsp_rtc_handle();
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_STATE, TAG, "RTC not started");
    struct tm tm;
    gmtime_r(&utc, &tm);
    return pcf85063_set_alarm(h, &tm);
}

esp_err_t hal_rtc_cancel_alarm(void)
{
    pcf85063_handle_t h = bsp_rtc_handle();
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_STATE, TAG, "RTC not started");
    return pcf85063_disable_alarm(h);
}

static void on_bsp_alarm(void *ctx)
{
    (void)ctx;
    if (s_cb) {
        s_cb(s_ctx);
    }
}

void hal_rtc_set_alarm_cb(hal_rtc_alarm_cb_t cb, void *ctx)
{
    s_ctx = ctx;
    s_cb = cb;
    bsp_rtc_set_alarm_cb(cb ? on_bsp_alarm : NULL, NULL);
}
