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

/** Pro licence (docs/10 §4, Pro edition only): locked, every Pro screen (launcher, tiles, home
 *  swipe, settings, complications, alerts) opens the unlock prompt ("pro.locked") instead.
 *  Default unlocked; app_main follows svc_license, the simulator its `pro_locked` command. */
void shell_set_pro_locked(bool locked);
bool shell_pro_locked(void);

/** True if id is a Pro screen or app (or one of its sub-screens, "<id>.*"). */
bool shell_is_pro_screen(const char *id);

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

typedef struct shell_app shell_app_t;
struct shell_app {
    const char *id;   // screen id opened with ui_nav_push_id() (same ids as complications)
    const char *name;
    const char *icon; // LV_SYMBOL_* (or any text in the theme font)
    uint32_t color;   // 0xRRGGBB icon background
    shell_app_kind_t kind;
    /** Opens the app instead of pushing the screen id (mini apps, mini_apps.h). NULL = push. */
    void (*open)(const shell_app_t *app);
    /** Long press in the launcher asks to uninstall, then calls this. NULL = cannot be removed. */
    void (*remove)(const shell_app_t *app);
    /** A picture shown instead of icon and color (round, any size: it is scaled), e.g. a mini
     *  app's icon.png. NULL = the symbol. */
    const lv_image_dsc_t *image;
};

#define SHELL_APPS_MAX   64 // 19 system apps + mini apps (APP_REG_MAX 40)
#define SHELL_RECENT_MAX 3

/** Add an app (the system apps are built in; mini apps come with the installer,
 *  mini_apps.h). app must stay valid until unregistered. ESP_ERR_INVALID_STATE if the id exists. */
esp_err_t shell_app_register(const shell_app_t *app);
/** Remove an app (and from the recent apps). An open launcher is not rebuilt: call
 *  shell_launcher_refresh() after a batch of changes. */
void shell_app_unregister(const char *id);
size_t shell_app_count(void);
const shell_app_t *shell_app_at(size_t index);
const shell_app_t *shell_app_find(const char *id);

/** Open app: push its screen and remember it as recent, or a toast while the
 *  screen does not exist yet. */
void shell_app_open(const shell_app_t *app);

/** Rebuild an open launcher (the app list changed). */
void shell_launcher_refresh(void);

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
