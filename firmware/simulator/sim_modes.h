/* DND / sleep / theater modes in the simulator: the same modes.c as svc_modes, in
 * memory, on the pinned clock (UTC). Quick settings reads and toggles them; the
 * script sets them by hand or schedules them. No screen-off for theater mode (no
 * svc_power here): it only shows in quick settings. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "modes.h"

#ifdef __cplusplus
extern "C" {
#endif

modes_state_t sim_modes_state(void);
void sim_modes_set(mode_id_t id, bool on);
void sim_modes_sched(mode_id_t id, const mode_sched_t *sched);

#ifdef __cplusplus
}
#endif
