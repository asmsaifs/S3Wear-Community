#pragma once

#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(SVC_WIFI_EVENT);

typedef enum {
    SVC_WIFI_EVT_STATE, // no data: the switch, state, network, error or saved list changed (svc_wifi_get())
} svc_wifi_event_t;

#ifdef __cplusplus
}
#endif
