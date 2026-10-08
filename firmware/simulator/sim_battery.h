/* Battery screens in the simulator (battery_apps.h backend): hal_sim's fake battery,
 * a demo 24 h history on the same battery_hist.c as svc_power, battery saver in
 * memory (quick settings shares it). No svc_power here: the script plays its events
 * (charger, low battery). */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Demo history (charged this morning, now 80 % and draining) and the backend. */
void sim_battery_init(void);
/* A new fake reading: level and USB power; posts the PMU events a change makes. */
void sim_battery_set(int percent, bool vbus);
bool sim_battery_saver(void);
void sim_battery_set_saver(bool on);

#ifdef __cplusplus
}
#endif
