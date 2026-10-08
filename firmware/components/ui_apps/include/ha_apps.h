// Home app and the Home tile (docs/03 F17, docs/04 §4l, P9-05): Home Assistant entities chosen on
// the phone. Portable (LVGL + ui_framework): the simulator builds it too. UI task only, like ui_nav.h.
//
// The screen calls no service: refreshes and commands go through a backend (app_main: svc_ha;
// simulator: sim_ha.c). What to show comes in with ha_apps_set() and is kept outside the screen,
// so the tile shows it too.
#pragma once

#include <stdint.h>
#include "ha_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*refresh)(void *ctx);                // read every state again; the answer comes as ha_apps_set()
    void (*command)(uint8_t index, void *ctx); // toggle or run entity index
    void *ctx;
} ha_backend_t;

/** Register the screen (shell_init() does this). */
void ha_apps_init(void);

/** Install the backend (copied). Without one nothing is refreshed and taps do nothing. */
void ha_apps_set_backend(const ha_backend_t *backend);

/** What to show changed (copied): refresh the Home app (id "home") and the Home tile if visible. */
void ha_apps_set(const ha_view_t *view);

#ifdef __cplusplus
}
#endif
