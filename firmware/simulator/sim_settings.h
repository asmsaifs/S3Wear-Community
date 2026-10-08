/* Settings app backend in the simulator (settings_apps.h): the same settings_store.c
 * as svc_settings, in memory (nothing is saved). The settings that have an effect
 * here are applied as app_main does: the clock format, the DND / sleep schedules
 * (sim_modes) and battery saver (sim_battery). */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Defaults for every setting, then install the backend. */
void sim_settings_init(void);
/* Script: set a setting by its NVS key ("disp_aod" 1, "tz" JST-9). False if the key or the value is invalid. */
bool sim_settings_set(const char *key, const char *value);
/* Script: true if the setting's value equals value (bool/int as text, string as is). */
bool sim_settings_is(const char *key, const char *value);

#ifdef __cplusplus
}
#endif
