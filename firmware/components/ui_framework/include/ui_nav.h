// Screen lifecycle and navigation stack (docs/04-ui-ux.md §3–4).
//
// Every function here runs on the UI task (or with lv_lock() held). Other tasks
// use s3w_ui_post(). The bottom of the stack is the home screen (watch face).
//
// Lifecycle of one screen:  on_create -> on_resume -> (on_pause <-> on_resume)* ->
//                            on_pause -> on_destroy
// on_pause runs when the screen is covered (push, full-screen alert) or the display
// goes off (ui_nav_set_active(false)); it must stop the screen's animations. Timers
// made with ui_screen_timer_create() are paused/resumed/deleted automatically.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum stack depth including the home screen. */
#define UI_NAV_MAX_DEPTH 8

typedef struct ui_screen ui_screen_t;

/** Screen flags (screen_def_t.flags). */
enum {
    UI_SCREEN_KEEP_ON = 1u << 0,       // no screen-off timeout while on top (svc_power hold)
    UI_SCREEN_FULLSCREEN = 1u << 1,    // no system overlays except full-screen alerts
    UI_SCREEN_NO_SWIPE_BACK = 1u << 2, // left-edge swipe does nothing (games, pickers)
};

typedef struct {
    const char *id; // unique, used by ui_nav_push_id() and in logs
    /** Build the UI under root (a full-screen container). args is only valid during the call. */
    void (*on_create)(ui_screen_t *s, lv_obj_t *root, const void *args);
    void (*on_resume)(ui_screen_t *s);  // visible again: re-subscribe to events
    void (*on_pause)(ui_screen_t *s);   // covered or display off: stop timers/animations
    void (*on_destroy)(ui_screen_t *s); // free resources; LVGL objects are deleted after this
    bool (*on_back)(ui_screen_t *s);    // true if handled (the screen is not popped)
    uint32_t flags;
    size_t state_size;                  // bytes of zeroed per-instance state, see ui_screen_state()
} screen_def_t;

/** Call after ui_theme_init(): makes home the stack root and shows it. If another
 *  module later replaces the active screen with auto-delete (console `lcd bars`,
 *  factory test), the stack is torn down (on_pause/on_destroy run) and depth is 0
 *  until ui_nav_init() is called again. */
esp_err_t ui_nav_init(const screen_def_t *home, const void *args);

/** Push a new screen (slide in from the right). ESP_ERR_NO_MEM if the stack is full. */
esp_err_t ui_nav_push(const screen_def_t *def, const void *args);

/** Side a pushed screen slides in from; it slides back out the same way when popped. */
typedef enum {
    UI_SLIDE_FROM_RIGHT = 0, // ui_nav_push(): apps, tiles
    UI_SLIDE_FROM_LEFT,      // launcher
    UI_SLIDE_FROM_TOP,       // quick settings
    UI_SLIDE_FROM_BOTTOM,    // notifications
} ui_slide_t;

/** ui_nav_push() with the side the screen comes from. */
esp_err_t ui_nav_push_slide(const screen_def_t *def, const void *args, ui_slide_t from);

/** Home panels (docs/04 §3): the screen a swipe on the home screen opens. dir is the
 *  finger direction (LV_DIR_LEFT/RIGHT/TOP/BOTTOM); NULL clears it. */
void ui_nav_set_home_swipe(lv_dir_t dir, const screen_def_t *def);

/** For the home screen's gesture handler: push the panel registered for dir (no
 *  args), sliding in from the side opposite to dir. False if none, if home is not
 *  the top screen or under a full-screen alert. */
bool ui_nav_home_swipe(lv_dir_t dir);

/** Push a screen registered with ui_nav_register(); ESP_ERR_NOT_FOUND if unknown. */
esp_err_t ui_nav_push_id(const char *id, const void *args);

/** Pop the top screen (slide out to the right). False if only home is left. */
bool ui_nav_pop(void);

/** Back key / edge swipe: dismiss a dismissable alert, else ask the top screen's
 *  on_back, else pop. False if nothing happened (already at home). */
bool ui_nav_back(void);

/** Pop everything above home (PWR key). */
void ui_nav_home(void);

/** Display on/off: pauses or resumes the top screen and the clock tick (svc_power). */
void ui_nav_set_active(bool active);
bool ui_nav_is_active(void);

/** cb runs (UI task) after every change of the stack, the top screen's visibility
 *  or the active state, e.g. to follow UI_SCREEN_KEEP_ON. One listener. */
void ui_nav_set_listener(void (*cb)(void *ctx), void *ctx);

size_t ui_nav_depth(void);
ui_screen_t *ui_nav_top(void);
/** Screen at depth index (0 = home), NULL if out of range. */
ui_screen_t *ui_nav_at(size_t index);

/** Registry for ui_nav_push_id() (console, scripts, deep links). Up to 32 entries. */
esp_err_t ui_nav_register(const screen_def_t *def);
const screen_def_t *ui_nav_find(const char *id);

// --- Screen instance -----------------------------------------------------------------

const screen_def_t *ui_screen_def(const ui_screen_t *s);
lv_obj_t *ui_screen_root(const ui_screen_t *s);
/** The zeroed state block of def->state_size bytes (NULL if 0). Freed after on_destroy. */
void *ui_screen_state(const ui_screen_t *s);
/** True between on_resume and on_pause. */
bool ui_screen_is_visible(const ui_screen_t *s);

/** An lv_timer owned by the screen: runs only while the screen is visible and is
 *  deleted with it. Up to 4 per screen; NULL if none left. Starts paused if hidden. */
lv_timer_t *ui_screen_timer_create(ui_screen_t *s, lv_timer_cb_t cb, uint32_t period_ms, void *user_data);

// --- Clock source -----------------------------------------------------------------------

/** Wall clock used by s3w_header and the home screen; default time(NULL). The
 *  simulator pins it for snapshot tests. */
void ui_clock_set_source(time_t (*now)(void));
time_t ui_clock_now(void);
/** 24 h ("09:05") or 12 h ("9:05") format; default 24 h. Rebinds every clock label. */
void ui_clock_set_24h(bool h24);
bool ui_clock_is_24h(void);
/** Format the current local time into buf (at least 8 bytes); "--:--" while unknown. */
void ui_clock_format(char *buf, size_t len);
/** Time known (svc_time "time valid"). While false every clock shows "--:--". Default true. */
void ui_clock_set_valid(bool valid);
bool ui_clock_is_valid(void);
/** Re-render every clock and re-align the minute tick (after the time, zone or format changed). */
void ui_clock_refresh(void);
/** Show obj only while the time is unknown (show_when_unknown) or only while it is known.
 *  Unbinds itself when obj is deleted. */
void ui_clock_bind_unknown(lv_obj_t *obj, bool show_when_unknown);
/** Keep label's text at the current time, updated on each minute while the nav is
 *  active (one wake-up per minute). Unbinds itself when the label is deleted. */
void ui_clock_bind_label(lv_obj_t *label);
/** cb(ctx) runs after every clock re-render: the minute tick, display on, a format or
 *  "time valid" change and ui_clock_refresh(). Up to 4 listeners (watch faces). */
esp_err_t ui_clock_add_listener(void (*cb)(void *ctx), void *ctx);
void ui_clock_remove_listener(void (*cb)(void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif
