#pragma once

#include <stdint.h>
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(SVC_SETTINGS_EVENT);

typedef enum {
    SVC_SETTINGS_EVT_CHANGED,  // payload svc_settings_evt_changed_t; read the new value with a getter
    SVC_SETTINGS_EVT_RESET,    // factory reset: every setting is back at its default; no payload
} svc_settings_event_t;

typedef struct {
    uint16_t id; // s3w_setting_t
} svc_settings_evt_changed_t;

#ifdef __cplusplus
}
#endif
