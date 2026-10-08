// Flashlight app (docs/03 F20, docs/04 §4h, P8-15). Portable (LVGL + ui_framework): the simulator
// builds it too. UI task only, like ui_nav.h.
//
// Id "flashlight": a full-white screen, a tap toggles red night mode. The screen stays on while it
// is shown (UI_SCREEN_KEEP_ON); full brightness goes through a backend (app_main: svc_power, the
// DISPLAY_BRIGHTNESS setting is never changed). It is opened by shell_app_open() (launcher, quick
// settings) and by the BOOT long press (flashlight_apps_open()).
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /** The flashlight opened (true) or closed (false): panel at full brightness / back to the user's. */
    void (*boost)(bool on, void *ctx);
    void *ctx;
} flashlight_backend_t;

/** Register the screen (shell_init() does this). */
void flashlight_apps_init(void);

/** Install the backend (copied). Without one, brightness is not changed. */
void flashlight_apps_set_backend(const flashlight_backend_t *backend);

/** Open the flashlight unless it is already the top screen (the BOOT long press shortcut). */
void flashlight_apps_open(void);

/** The flashlight is the top screen. */
bool flashlight_apps_active(void);

#ifdef __cplusplus
}
#endif
