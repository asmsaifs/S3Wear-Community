#include "hal_pmu.h"

#include "bsp_s3w.h"
#include "esp_check.h"

static const char *TAG = "hal_pmu";

static hal_pmu_event_cb_t s_cb;
static void *s_ctx;
static bool s_registered;

esp_err_t hal_pmu_read_battery(hal_battery_t *out)
{
    axp2101_handle_t h = bsp_pmu_handle();
    ESP_RETURN_ON_FALSE(h && out, ESP_ERR_INVALID_STATE, TAG, "PMU not started");
    axp2101_status_t st;
    ESP_RETURN_ON_ERROR(axp2101_read_status(h, &st), TAG, "status");
    *out = (hal_battery_t){
        .percent = st.battery_present ? st.battery_pct : -1,
        .mv = st.battery_present ? st.vbat_mv : 0,
        .charging = st.charging,
        .vbus = st.vbus_good,
    };
    return ESP_OK;
}

// BSP_PMU_EVENT arrives on the default event loop; forward the ones the HAL exposes.
static void on_bsp_pmu(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    static const int8_t map[] = {
        [BSP_PMU_EVT_VBUS_IN] = HAL_PMU_EVT_VBUS_IN,       [BSP_PMU_EVT_VBUS_OUT] = HAL_PMU_EVT_VBUS_OUT,
        [BSP_PMU_EVT_BAT_IN] = -1,                         [BSP_PMU_EVT_BAT_OUT] = -1,
        [BSP_PMU_EVT_CHG_START] = HAL_PMU_EVT_CHG_START,   [BSP_PMU_EVT_CHG_DONE] = HAL_PMU_EVT_CHG_DONE,
        [BSP_PMU_EVT_PKEY_SHORT] = HAL_PMU_EVT_PKEY_SHORT, [BSP_PMU_EVT_PKEY_LONG] = HAL_PMU_EVT_PKEY_LONG,
        [BSP_PMU_EVT_BAT_LOW] = HAL_PMU_EVT_BAT_LOW,       [BSP_PMU_EVT_OVERTEMP] = -1,
    };
    const hal_pmu_event_cb_t cb = s_cb;
    if (cb && id >= 0 && id < (int32_t)(sizeof map / sizeof map[0]) && map[id] >= 0) {
        cb((hal_pmu_event_t)map[id], s_ctx);
    }
}

esp_err_t hal_pmu_set_event_cb(hal_pmu_event_cb_t cb, void *ctx)
{
    s_ctx = ctx;
    s_cb = cb;
    if (!s_registered) {
        ESP_RETURN_ON_ERROR(esp_event_handler_register(BSP_PMU_EVENT, ESP_EVENT_ANY_ID, on_bsp_pmu, NULL), TAG,
                            "register");
        s_registered = true;
    }
    return ESP_OK;
}

esp_err_t hal_pmu_power_off(void)
{
    axp2101_handle_t h = bsp_pmu_handle();
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_STATE, TAG, "PMU not started");
    return axp2101_shutdown(h);
}
