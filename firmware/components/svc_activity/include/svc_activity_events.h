#pragma once

#include <stdint.h>
#include "activity_day.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(SVC_ACTIVITY_EVENT);

typedef enum {
    SVC_ACTIVITY_EVT_STEPS,   // svc_activity_evt_steps_t: today's steps changed (at most once per FIFO batch)
    SVC_ACTIVITY_EVT_GOAL,    // svc_activity_evt_goal_t: a daily goal was reached (once per goal and day)
    SVC_ACTIVITY_EVT_DAY_END, // activity_summary_t of the day that ended (local midnight)
} svc_activity_event_t;

typedef struct {
    uint32_t steps;
    uint32_t step_goal;
} svc_activity_evt_steps_t;

typedef enum { SVC_ACTIVITY_GOAL_STEPS = 0, SVC_ACTIVITY_GOAL_ACTIVE_MIN } svc_activity_goal_t;

typedef struct {
    uint8_t goal;   // svc_activity_goal_t
    uint32_t value; // the goal (steps or minutes)
} svc_activity_evt_goal_t;

#ifdef __cplusplus
}
#endif
