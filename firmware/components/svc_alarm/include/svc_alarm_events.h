#pragma once

#include <stdint.h>
#include "alarm_sched.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(SVC_ALARM_EVENT);

typedef enum {
    SVC_ALARM_EVT_CHANGED,  // no payload: alarms, snooze or timers changed (re-read them)
    SVC_ALARM_EVT_RING,     // svc_alarm_evt_ring_t: an alarm or timer starts ringing
    SVC_ALARM_EVT_RING_END, // svc_alarm_evt_ring_end_t: the ringing stopped
} svc_alarm_event_t;

typedef enum {
    SVC_ALARM_RING_ALARM,
    SVC_ALARM_RING_TIMER,
} svc_alarm_ring_kind_t;

typedef struct {
    uint8_t kind;        // svc_alarm_ring_kind_t
    uint8_t id;          // alarm or timer id
    uint8_t snooze_min;  // alarm: snooze length
    bool snoozed;        // alarm: a snooze ending
    int64_t at;          // alarm: UTC s of the occurrence
    uint32_t duration_ms; // timer: as started
    char label[ALARM_LABEL_MAX + 1];
} svc_alarm_evt_ring_t;

typedef enum {
    SVC_ALARM_END_DISMISS,
    SVC_ALARM_END_SNOOZE,
    SVC_ALARM_END_FLIP,    // flipped face down: snoozed
    SVC_ALARM_END_TIMEOUT, // nobody reacted: snoozed (alarm, up to SVC_ALARM_AUTO_SNOOZES) or stopped
    SVC_ALARM_END_REPLACED, // an alarm took over from a timer
} svc_alarm_end_reason_t;

typedef struct {
    uint8_t kind;   // svc_alarm_ring_kind_t
    uint8_t id;
    uint8_t reason; // svc_alarm_end_reason_t
    uint8_t snooze_min; // when snoozed
} svc_alarm_evt_ring_end_t;

#ifdef __cplusplus
}
#endif
