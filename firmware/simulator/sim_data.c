#include "sim_data.h"

#include <stdio.h>

#include "hal.h"
#include "sim_script.h"
#include "wf_engine.h"

void sim_data_battery(void)
{
    hal_battery_t b;
    if (hal_pmu_read_battery(&b) != ESP_OK) {
        return;
    }
    wf_data_t *d = wf_data_edit();
    d->battery_pct = b.percent;
    d->charging = b.charging;
    wf_data_changed(WF_DATA_BATTERY);
}

void sim_data_demo(void)
{
    const time_t now = sim_fixed_now();
    wf_data_t *d = wf_data_edit();
    d->steps = 6420;
    d->steps_goal = 10000;
    d->weather_valid = true;
    d->temp_c = 18;
    d->temp_lo_c = 12;
    d->temp_hi_c = 21;
    d->weather = WF_WEATHER_CLOUDY;
    d->phone_battery_pct = 64;
    d->event_valid = true;
    snprintf(d->event_title, sizeof d->event_title, "Design review");
    d->event_start = now - now % 3600 + 3600 + 30 * 60; /* next hour + 30 min */
    d->sunrise_min = 6 * 60 + 12;
    d->sunset_min = 18 * 60 + 5;
    d->world_valid = true;
    snprintf(d->world_label, sizeof d->world_label, "TOKYO");
    d->world_utc_offset_s = 9 * 3600;
    d->heart_bpm = 72;
    d->alarm_next = now - now % 86400 + 86400 + 7 * 3600; /* 07:00 tomorrow */
    d->timer_end = 0;
    d->notifications = 3;
    wf_data_changed(WF_DATA_ALL);
}

void sim_data_clear(void)
{
    wf_data_init(wf_data_edit());
    wf_data_changed(WF_DATA_ALL);
}
