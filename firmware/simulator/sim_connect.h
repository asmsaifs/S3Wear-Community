/* Settings > Connections in the simulator (connect_apps.h): the backend keeps a fake link
 * state, and the script plays the phone's side. */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Install the backend: Bluetooth on, paired, connected, synced 5 min ago; Wi-Fi off, "Home" saved. */
void sim_connect_init(void);

/* Bluetooth on / off (quick settings); the Connections page refreshes. */
bool sim_connect_bluetooth(void);
void sim_connect_set_bluetooth(bool on);

/* Wi-Fi on / off (quick settings); the Connections and Wi-Fi pages refresh. */
bool sim_connect_wifi(void);
void sim_connect_set_wifi(bool on);

/* Script commands starting with "link" or "wifi" (see firmware/test/ui/README.md). */
bool sim_connect_cmd(const char *cmd, const char *arg);

#ifdef __cplusplus
}
#endif
