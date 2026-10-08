#pragma once

#include <stdint.h>
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(SVC_SENSORS_EVENT);

typedef enum {
    SVC_SENSORS_EVT_RAISE, // svc_sensors_evt_raise_t, raise-to-wake detected (the screen is being woken)
    SVC_SENSORS_EVT_FLIP,  // no payload: flipped face down while svc_sensors_watch_flip() is on
} svc_sensors_event_t;

typedef struct {
    uint16_t window_ms; // from the wake-on-motion interrupt to the detection
} svc_sensors_evt_raise_t;

#ifdef __cplusplus
}
#endif
