/* Clock apps backend in the simulator (clock_apps.h): alarms and timers in memory
 * over the same alarm_sched/timer_set as svc_alarm. Alarms use the pinned clock
 * (UTC); timers the LVGL tick, so they run down with `wait` and ring when done.
 * Alarms ring only from the script (`ring alarm`): the pinned clock never reaches
 * them. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Install the backend with two demo alarms (07:00 daily, 08:30 weekends off) and
 * the world clock cities Tokyo, London, New York. */
void sim_clock_init(void);

/* Add an alarm; days: ALARM_DAYS_* bits. False if full or invalid. */
bool sim_clock_add_alarm(int hour, int minute, uint8_t days, const char *label);
/* Remove every alarm. */
void sim_clock_clear_alarms(void);
/* Start a countdown of ms. */
bool sim_clock_start_timer(uint32_t ms);
/* Ring the first enabled alarm, or finish and ring the first running timer. */
bool sim_clock_ring(bool alarm);

#ifdef __cplusplus
}
#endif
