#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(SVC_POWER_EVENT);

typedef enum {
    SVC_POWER_EVT_STATE,       // svc_power_evt_state_t, after the new state is applied
    SVC_POWER_EVT_BATTERY,     // svc_power_battery_t, when %, charging or USB power changes
    SVC_POWER_EVT_BATTERY_LOW, // svc_power_evt_battery_low_t, once per threshold while discharging
} svc_power_event_t;

typedef struct {
    uint8_t state; // power_state_t
    uint8_t prev;  // power_state_t
    bool saver;
} svc_power_evt_state_t;

typedef struct {
    int8_t percent; // -1 = no battery
    uint16_t mv;
    bool charging;
    bool vbus;
} svc_power_battery_t;

typedef struct {
    int8_t percent;
    uint8_t threshold; // 15 (warn), 10 (offer saver) or 3 (watch-only), docs/03 F5
} svc_power_evt_battery_low_t;

#ifdef __cplusplus
}
#endif
