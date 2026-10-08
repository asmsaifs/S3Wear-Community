// System overlays on LVGL's top layer (docs/04-ui-ux.md §3): toast, banner and
// full-screen alert. UI task only, like ui_nav.h.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_TOAST_MS_DEFAULT  2000
#define UI_BANNER_MS_DEFAULT 4000

/** Short message pill at the top. Replaces a visible toast. ms = 0 -> default. */
void ui_toast_show(const char *text, uint32_t ms);

typedef struct {
    const char *icon;   // LV_SYMBOL_* or NULL
    /** Image shown instead of icon (e.g. an app icon, ≤ 48 px); NULL = none. Not copied:
     *  it must stay valid until the banner closes or another one replaces it. */
    const lv_image_dsc_t *image;
    const char *title;
    const char *body;   // may be NULL; long text is cut with "..."
    /** Phone-rendered title and text (A8 coverage, drawn in the text colour, scaled to the
     *  text column; rows past the banner are cut) shown instead of title and body; NULL =
     *  none. Not copied, like image. */
    const lv_image_dsc_t *text_image;
    uint32_t ms;        // 0 -> UI_BANNER_MS_DEFAULT
    void (*on_tap)(void *ctx); // tap on the banner (it closes first); may be NULL
    void *ctx;
} ui_banner_t;

/** Banner notification at the top. Replaces a visible banner. Not shown over a
 *  screen with UI_SCREEN_FULLSCREEN or under a full-screen alert. Returns the banner's
 *  id (never 0), or 0 if it was not shown. */
uint32_t ui_banner_show(const ui_banner_t *banner);
/** Replace the title and body of banner id, if still visible, with text_image (see
 *  ui_banner_t) and show it for its full time again. Returns false if it is gone. */
bool ui_banner_set_text_image(uint32_t id, const lv_image_dsc_t *text_image);
/** Fade out the visible banner (e.g. the list it announces was opened). */
void ui_banner_dismiss(void);

/** Alert priority: a higher one replaces a lower one; an equal or lower one waits
 *  until the current one is dismissed (one waiting slot, newest wins). */
typedef enum {
    UI_ALERT_PRIO_LOW = 0, // e.g. low battery
    UI_ALERT_PRIO_NORMAL,  // e.g. timer done
    UI_ALERT_PRIO_HIGH,    // e.g. alarm, incoming call
} ui_alert_prio_t;

typedef struct {
    const char *icon;    // LV_SYMBOL_* or NULL
    const char *title;
    const char *body;    // may be NULL
    uint32_t accent;     // 0xRRGGBB for icon and primary button, 0 -> theme accent
    const char *primary;   // button labels; NULL hides the button
    const char *secondary;
    /** Called after the alert is closed. button: 0 primary, 1 secondary, -1 back key. */
    void (*on_result)(int button, void *ctx);
    void *ctx;
    ui_alert_prio_t prio;
    bool back_dismisses; // BACK key closes it (result -1); false for alarms and calls
} ui_alert_t;

/** Full-screen alert above everything; pauses the top screen. Strings are copied. */
void ui_alert_show(const ui_alert_t *alert);
/** Close the current alert without a result callback (e.g. the call was cancelled). */
void ui_alert_dismiss(void);
/** Withdraw the alert shown or waiting with this on_result and ctx (e.g. the phone
 *  gave up pairing), without a result callback. Other alerts stay. */
void ui_alert_cancel(void (*on_result)(int button, void *ctx), void *ctx);
bool ui_alert_is_active(void);

/** Remove toast, banner and alerts (e.g. before a screenshot or on factory reset). */
void ui_overlay_clear(void);

#ifdef __cplusplus
}
#endif
