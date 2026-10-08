/* Watch face data in the simulator: a fixed demo set standing in for the phone link
 * and services that do not run here yet, plus the battery from hal_sim. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Battery from hal_pmu (hal_sim) into the face data. */
void sim_data_battery(void);
/* Demo values for every complication (steps, weather, next event, ...). */
void sim_data_demo(void);
/* Everything unknown, as on a watch with no phone and no services. */
void sim_data_clear(void);

#ifdef __cplusplus
}
#endif
