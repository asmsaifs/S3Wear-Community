// Screens built into ui_framework: the power menu and the widget gallery used by
// snapshot tests. The home screen (the watch face) comes from above: ui_set_home().
#pragma once

#include "ui_nav.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Power menu actions, passed to ui_power_menu_args_t.on_action. */
typedef enum {
    UI_POWER_ACTION_OFF,
    UI_POWER_ACTION_RESTART,
    UI_POWER_ACTION_SAVER_ON,
    UI_POWER_ACTION_SAVER_OFF,
    UI_POWER_ACTION_WATCH_ONLY,
} ui_power_action_t;

/** Push args for ui_power_menu_screen (copied). NULL args: no battery value, and
 *  every action only shows a toast (simulator, console `ui push power`). */
typedef struct {
    void (*on_action)(ui_power_action_t action, void *ctx); // UI task; confirmed already
    void *ctx;
    bool saver;
    bool charging;
    int8_t battery_pct; // -1 = unknown
} ui_power_menu_args_t;

/** Power menu (id "power"): battery, saver toggle, watch-only, restart, power off. */
extern const screen_def_t ui_power_menu_screen;

/** Register "gallery" and its pages ("gallery.buttons", "gallery.lists", ...) with
 *  ui_nav_register(). Call once before pushing them by id. */
void ui_gallery_register(void);

/** The screen at the bottom of the stack (the watch face, watchfaces/wf_engine.h). */
void ui_set_home(const screen_def_t *home);

/** Start the UI: theme already installed and ui_set_home() called; registers the
 *  built-in screens (gallery, power menu) and shows the home screen (fades in over
 *  the active screen). Can be called again after the stack was torn down (see
 *  ui_nav_init()). ESP_ERR_INVALID_STATE without a home screen. */
esp_err_t ui_start(void);

#ifdef __cplusplus
}
#endif
