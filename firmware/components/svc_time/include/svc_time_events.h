#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(SVC_TIME_EVENT);

typedef enum {
    SVC_TIME_EVT_CHANGED, // svc_time_evt_changed_t: the clock, the zone or the 12/24 h format changed
} svc_time_event_t;

/** svc_time_evt_changed_t.what bits. */
#define SVC_TIME_CHANGED_CLOCK  (1u << 0) // set, synced or stepped by the RTC discipline
#define SVC_TIME_CHANGED_ZONE   (1u << 1) // new zone, or a DST transition
#define SVC_TIME_CHANGED_FORMAT (1u << 2) // 12/24 h

typedef struct {
    uint8_t what;         // SVC_TIME_CHANGED_* bits
    uint8_t source;       // svc_time_source_t
    bool valid;           // false: "time unknown" (RTC lost power, never synced)
    bool h24;
    int32_t utc_offset_s; // local - UTC now, seconds
} svc_time_evt_changed_t;

#ifdef __cplusplus
}
#endif
