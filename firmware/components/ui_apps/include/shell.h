// System shell around the watch face (docs/04-ui-ux.md §3, docs/03 F3/F4, P3-06):
// quick settings (swipe down), notifications (swipe up, placeholder until P6), tiles
// (swipe left) and the launcher (swipe right or BOOT on the home screen) with the app
// registry. Portable (LVGL + ui_framework + watchfaces): the simulator builds it too.
// UI task only, like ui_nav.h.
//
// The screens call no service: quick settings reads and changes state through a
// backend (app_main: svc_settings, svc_power), the launcher reports its layout
// change through a listener, so the simulator can run them without services.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "ui_nav.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Register the shell screens ("qs", "notifications", "tiles", "launcher"), the clock
 *  apps (clock_apps.h) and the home swipes. Call once on the UI task after wf_init(),
 *  before ui_start(). */
void shell_init(void);

// --- Quick settings (id "qs") -----------------------------------------------------------

/** Quick settings buttons, in grid order (docs/03 F4). */
typedef enum {
    SHELL_QS_DND,
    SHELL_QS_THEATER,
    SHELL_QS_SLEEP,
    SHELL_QS_AOD,
    SHELL_QS_WIFI,
    SHELL_QS_BLUETOOTH,
    SHELL_QS_SILENT,
    SHELL_QS_FLASHLIGHT,
    SHELL_QS_SAVER,
    SHELL_QS_FIND_PHONE,
    SHELL_QS_SETTINGS,
    SHELL_QS_COUNT,
} shell_qs_item_t;

/** State shown when quick settings opens. */
typedef struct {
    uint32_t available; // bit (1u << item): the item works; others are dim and say "not available yet"
    uint32_t on;        // bit (1u << item): toggle is on
    uint8_t brightness; // %, 5..100
    int8_t battery_pct; // -1 unknown
    bool charging;
    bool phone_connected;
} shell_qs_state_t;

typedef struct {
    /** Fill st when quick settings opens (and when it is shown again). */
    void (*read)(shell_qs_state_t *st, void *ctx);
    /** A toggle changed (the button already shows the new state). Actions
     *  (flashlight, find phone, settings) arrive with on = true. */
    void (*toggle)(shell_qs_item_t item, bool on, void *ctx);
    /** Brightness slider released at pct. */
    void (*brightness)(uint8_t pct, void *ctx);
    void *ctx;
} shell_qs_backend_t;

/** Install the backend (copied). Without one, every toggle works in memory only
 *  (simulator) and brightness is 60 %. */
void shell_qs_set_backend(const shell_qs_backend_t *backend);

/** Name of an item ("Do not disturb", ...), for toasts and logs. */
const char *shell_qs_name(shell_qs_item_t item);

// --- Launcher (id "launcher") -------------------------------------------------------------

typedef enum {
    SHELL_APP_SYSTEM,
    SHELL_APP_MINI, // WASM mini apps (P8)
    SHELL_APP_GAME,
} shell_app_kind_t;

typedef struct {
    const char *id;   // screen id opened with ui_nav_push_id() (same ids as complications)
    const char *name;
    const char *icon; // LV_SYMBOL_*
    uint32_t color;   // 0xRRGGBB icon background
    shell_app_kind_t kind;
} shell_app_t;

#define SHELL_APPS_MAX   48
#define SHELL_RECENT_MAX 3

/** Add an app (the system apps are built in; mini apps come with the installer,
 *  P8-05). app must stay valid. ESP_ERR_INVALID_STATE if the id exists. */
esp_err_t shell_app_register(const shell_app_t *app);
size_t shell_app_count(void);
const shell_app_t *shell_app_at(size_t index);
const shell_app_t *shell_app_find(const char *id);

/** Open app: push its screen and remember it as recent, or a toast while the
 *  screen does not exist yet. */
void shell_app_open(const shell_app_t *app);

/** Launcher layout: list with sections (default) or honeycomb grid. Applies the next
 *  time the launcher is built (an open launcher rebuilds at once). */
void shell_launcher_set_grid(bool grid);
bool shell_launcher_grid(void);

/** cb runs when the user switched the layout in the launcher (app_main saves the
 *  LAUNCHER_GRID setting). One listener. */
void shell_launcher_set_listener(void (*cb)(bool grid, void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif
