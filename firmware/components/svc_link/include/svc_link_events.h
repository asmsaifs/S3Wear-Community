#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(SVC_LINK_EVENT);

typedef enum {
    SVC_LINK_EVT_STATE, // svc_link_evt_state_t: the companion session went up or down
} svc_link_event_t;

typedef struct {
    bool up; // the companion is connected over a secured link and S3W frames flow
} svc_link_evt_state_t;

#ifdef __cplusplus
}
#endif
