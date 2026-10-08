#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int8_t percent;    // fuel gauge 0..100, -1 if no battery
    uint16_t mv;       // battery voltage, 0 if no battery
    bool charging;
    bool vbus;         // USB power present
} hal_battery_t;

esp_err_t hal_pmu_read_battery(hal_battery_t *out);

typedef enum {
    HAL_PMU_EVT_VBUS_IN,
    HAL_PMU_EVT_VBUS_OUT,
    HAL_PMU_EVT_CHG_START,
    HAL_PMU_EVT_CHG_DONE,
    HAL_PMU_EVT_PKEY_SHORT,
    HAL_PMU_EVT_PKEY_LONG,
    HAL_PMU_EVT_BAT_LOW,
} hal_pmu_event_t;

/** Charger / power-key events. Runs in an event-loop context: keep it short. */
typedef void (*hal_pmu_event_cb_t)(hal_pmu_event_t evt, void *ctx);
esp_err_t hal_pmu_set_event_cb(hal_pmu_event_cb_t cb, void *ctx);

/** Cut all rails (the PWR key turns the watch back on). Does not return on success. */
esp_err_t hal_pmu_power_off(void);

#ifdef __cplusplus
}
#endif
