// Activity UI glue (P5-01). svc_activity's step events arrive on the UI task (s3w_ui_subscribe).
#include "activity_ui.h"

#include "esp_event.h"
#include "s3w_event.h"
#include "svc_activity.h"
#include "wf_engine.h"

static void set(uint32_t steps, uint32_t goal)
{
    wf_data_t *d = wf_data_edit();
    if (d->steps == (int32_t)steps && d->steps_goal == (int32_t)goal) {
        return;
    }
    d->steps = (int32_t)steps;
    d->steps_goal = (int32_t)goal;
    wf_data_changed(WF_DATA_STEPS);
}

// UI task.
static void on_steps(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    (void)id;
    if (len >= sizeof(svc_activity_evt_steps_t)) {
        const svc_activity_evt_steps_t *e = data;
        set(e->steps, e->step_goal);
    }
}

void activity_ui_start(void)
{
    s3w_ui_subscribe(SVC_ACTIVITY_EVENT, SVC_ACTIVITY_EVT_STEPS, on_steps, NULL, NULL);
    svc_activity_state_t st;
    svc_activity_get(&st); // restored totals, before the first batch
    set(st.today.steps, st.today.step_goal);
}
