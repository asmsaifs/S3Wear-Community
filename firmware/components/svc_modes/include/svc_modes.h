// Modes service (docs/02-firmware-architecture.md §7, docs/03 F2/F4/F7, P3-08): do not
// disturb, sleep mode and theater mode, by hand and on weekly schedules.
//
// DND and sleep mode are kept in the DND / SLEEP_MODE settings, their schedules in
// DND_* / SLEEP_* (days 0 = no schedule). Theater mode is not kept: a restart ends it.
// Turning a scheduled mode off inside its window skips the rest of that window.
//
// Consumers: svc_power (dark: no AOD, no tap or raise wake; quiet: no notification
// wake; theater on: screen off now), svc_sensors (raise off while dark), and later
// svc_audio (P3-11) and svc_notify (P6) for sounds and vibration. Alarms ignore all
// modes. API calls are safe from any task, including the UI task.
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "modes.h"
#include "svc_modes_events.h"

#ifdef __cplusplus
extern "C" {
#endif

/** After settings and svc_time, before svc_power and svc_sensors (they read the state at start). */
esp_err_t svc_modes_start(void);

/** Current state (all off before svc_modes_start). */
void svc_modes_get(modes_state_t *out);

/** Turn a mode on or off by hand (quick settings, console). */
esp_err_t svc_modes_set(mode_id_t id, bool on);

/** Schedule of MODE_DND or MODE_SLEEP (stored in the settings; days 0 = off). */
esp_err_t svc_modes_get_sched(mode_id_t id, mode_sched_t *out);
esp_err_t svc_modes_set_sched(mode_id_t id, const mode_sched_t *sched);

typedef struct {
    modes_t m;              // by hand, skips, schedules
    modes_state_t state;
    int64_t next_edge;      // UTC s of the next schedule edge (0 = none)
    uint32_t changes;       // published state changes since boot
} svc_modes_status_t;

void svc_modes_get_status(svc_modes_status_t *out);

#ifdef __cplusplus
}
#endif
