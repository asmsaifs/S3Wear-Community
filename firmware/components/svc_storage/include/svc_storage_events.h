#pragma once

#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(SVC_STORAGE_EVENT);

typedef enum {
    SVC_STORAGE_EVT_SD_MOUNTED,
    SVC_STORAGE_EVT_SD_REMOVED, // unmounted because the card stopped answering
    SVC_STORAGE_EVT_SD_UNMOUNTED,
} svc_storage_event_t;

#ifdef __cplusplus
}
#endif
