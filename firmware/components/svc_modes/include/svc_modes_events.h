#pragma once

#include "esp_event.h"
#include "modes.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(SVC_MODES_EVENT);

typedef enum {
    SVC_MODES_EVT_CHANGED, // modes_state_t: a mode turned on or off (by hand or schedule)
} svc_modes_event_t;

#ifdef __cplusplus
}
#endif
