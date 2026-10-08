// PMU: AXP2101 on the shared bus.
//
// The AXP2101 IRQ pin is not wired to the SoC (docs/01-hardware.md §2), so IRQ
// status is read (a) right after every PWR key edge on GPIO10 and (b) every
// PMU_POLL_MS to catch VBUS/battery/charger changes. Poll period 2 s: one 3-byte
// I2C read in the timer task, ~0.2 ms awake; "USB plugged" shows up within 2 s.
// Changes are posted as BSP_PMU_EVENT on the default esp_event loop.
#include "bsp_s3w.h"

#include "bsp_s3w_pins.h"
#include "bsp_s3w_priv.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "bsp_pmu";

ESP_EVENT_DEFINE_BASE(BSP_PMU_EVENT);

#define PMU_POLL_MS 2000

// 400 mAh cell (docs/01-hardware.md §4): CC <= 0.5 C, 25 mA termination, 4.2 V.
static const axp2101_charger_cfg_t k_charger = {
    .cc_ma = 200,
    .term_ma = 25,
    .precharge_ma = 50,
    .cv_mv = 4200,
};

static const struct {
    uint32_t irq;
    bsp_pmu_event_t evt;
} k_irq_map[] = {
    {AXP2101_IRQ_VBUS_INSERT, BSP_PMU_EVT_VBUS_IN},
    {AXP2101_IRQ_VBUS_REMOVE, BSP_PMU_EVT_VBUS_OUT},
    {AXP2101_IRQ_BAT_INSERT, BSP_PMU_EVT_BAT_IN},
    {AXP2101_IRQ_BAT_REMOVE, BSP_PMU_EVT_BAT_OUT},
    {AXP2101_IRQ_CHG_START, BSP_PMU_EVT_CHG_START},
    {AXP2101_IRQ_CHG_DONE, BSP_PMU_EVT_CHG_DONE},
    {AXP2101_IRQ_PKEY_SHORT, BSP_PMU_EVT_PKEY_SHORT},
    {AXP2101_IRQ_PKEY_LONG, BSP_PMU_EVT_PKEY_LONG},
    {AXP2101_IRQ_SOC_WARN1, BSP_PMU_EVT_BAT_LOW},
    {AXP2101_IRQ_DIE_OVERTEMP, BSP_PMU_EVT_OVERTEMP},
};

static axp2101_handle_t s_pmu;
static TimerHandle_t s_poll_timer;

void bsp_pmu_poll(void)
{
    if (!s_pmu) {
        return;
    }
    uint32_t irq = 0;
    if (axp2101_irq_read_clear(s_pmu, &irq) != ESP_OK || irq == 0) {
        return;
    }
    for (size_t i = 0; i < sizeof k_irq_map / sizeof k_irq_map[0]; i++) {
        if (irq & k_irq_map[i].irq) {
            esp_event_post(BSP_PMU_EVENT, k_irq_map[i].evt, NULL, 0, 0);
        }
    }
}

static void poll_timer_cb(TimerHandle_t t)
{
    (void)t;
    bsp_pmu_poll();
}

esp_err_t bsp_pmu_start(void)
{
    ESP_RETURN_ON_FALSE(bsp_i2c_bus(), ESP_ERR_INVALID_STATE, TAG, "bsp_init_early first");
    if (s_pmu) {
        return ESP_OK;
    }
    esp_err_t err = esp_event_loop_create_default();
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "event loop");

    ESP_RETURN_ON_ERROR(axp2101_new(bsp_i2c_bus(), BSP_I2C_ADDR_AXP2101, BSP_I2C_FREQ_HZ, &s_pmu), TAG, "axp2101");
    ESP_RETURN_ON_ERROR(axp2101_adc_setup(s_pmu, false), TAG, "adc"); // battery kit has no NTC
    ESP_RETURN_ON_ERROR(axp2101_charger_config(s_pmu, &k_charger), TAG, "charger");
    ESP_RETURN_ON_ERROR(axp2101_pkey_config(s_pmu, AXP2101_PKEY_OFF_6S), TAG, "pkey");

    uint32_t mask = 0;
    for (size_t i = 0; i < sizeof k_irq_map / sizeof k_irq_map[0]; i++) {
        mask |= k_irq_map[i].irq;
    }
    ESP_RETURN_ON_ERROR(axp2101_irq_set_mask(s_pmu, mask), TAG, "irq");

    uint8_t on_src = 0;
    uint8_t off_src = 0;
    if (axp2101_power_sources(s_pmu, &on_src, &off_src) == ESP_OK) {
        ESP_LOGI(TAG, "power-on source 0x%02X, last power-off source 0x%02X", on_src, off_src);
    }

    s_poll_timer = xTimerCreate("pmu_poll", pdMS_TO_TICKS(PMU_POLL_MS), pdTRUE, NULL, poll_timer_cb);
    ESP_RETURN_ON_FALSE(s_poll_timer && xTimerStart(s_poll_timer, 0) == pdPASS, ESP_ERR_NO_MEM, TAG, "timer");
    return ESP_OK;
}

axp2101_handle_t bsp_pmu_handle(void)
{
    return s_pmu;
}

const char *bsp_pmu_event_name(bsp_pmu_event_t evt)
{
    static const char *const k_names[] = {
        [BSP_PMU_EVT_VBUS_IN] = "VBUS inserted",       [BSP_PMU_EVT_VBUS_OUT] = "VBUS removed",
        [BSP_PMU_EVT_BAT_IN] = "battery inserted",     [BSP_PMU_EVT_BAT_OUT] = "battery removed",
        [BSP_PMU_EVT_CHG_START] = "charging started",  [BSP_PMU_EVT_CHG_DONE] = "charging done",
        [BSP_PMU_EVT_PKEY_SHORT] = "PWR short press",  [BSP_PMU_EVT_PKEY_LONG] = "PWR long press",
        [BSP_PMU_EVT_BAT_LOW] = "battery low warning", [BSP_PMU_EVT_OVERTEMP] = "die over-temperature",
    };
    return (unsigned)evt < sizeof k_names / sizeof k_names[0] ? k_names[evt] : "?";
}
