// Notifications UI (docs/03 F7, docs/04-ui-ux.md §4b/§4g, P4-07): the list ("notifications",
// swipe up on the face), the detail screen ("notifications.detail") and the banner for a new
// notification. Portable (LVGL + ui_framework + the pure notify_store.h types): the simulator
// builds it too. UI task only, like ui_nav.h.
//
// The screens call no service: they read and act through a backend (app_main: svc_notify;
// simulator: an in-memory notify_store) and copy what they show (text, icons, bitmaps), so the
// backend may drop an entry at any time. The app reports back with notif_apps_changed(),
// notif_apps_posted() and notif_apps_action_done().
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "notify_store.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NOTIF_ICON_SIZE  48
#define NOTIF_ICON_BYTES (NOTIF_ICON_SIZE * NOTIF_ICON_SIZE * 3) // RGB565A8

typedef struct {
    size_t (*count)(void *ctx);
    /** Copy of the i-th newest; false past the end. */
    bool (*get)(size_t i, notify_entry_t *out, void *ctx);
    bool (*find)(uint32_t nid, notify_entry_t *out, void *ctx);
    /** Copy an app icon (NOTIF_ICON_BYTES, RGB565A8); false: none (yet). */
    bool (*icon)(uint32_t icon_hash, uint8_t *out, size_t cap, void *ctx);
    /** Copy the phone-rendered text of nid as A8; returns rows (0 = none), *w / *h = size. */
    uint16_t (*bitmap)(uint32_t nid, uint8_t *a8, size_t cap, uint16_t max_rows, uint16_t *w, uint16_t *h,
                       void *ctx);
    /** Run an action on the phone; the answer comes back through notif_apps_action_done(). */
    void (*action)(uint32_t nid, uint32_t action_id, void *ctx);
    /** Remove here and on the phone. */
    void (*dismiss)(uint32_t nid, void *ctx);
    void (*clear)(void *ctx);
    void *ctx;
} notif_backend_t;

/** Register the detail screen (shell_init() does this; the list is a shell panel). */
void notif_apps_init(void);

/** Install the backend (copied). Without one the list is empty. */
void notif_apps_set_backend(const notif_backend_t *backend);

/** The list, an icon or a bitmap changed: rebuild the visible notification screen and the
 *  face's count, on the next LVGL timer run. */
void notif_apps_changed(void);

/** A new notification that alerts: banner over the current screen (not over the
 *  notification screens, full-screen apps or alerts); a tap opens its detail. */
void notif_apps_posted(uint32_t nid);

/** The phone ran (ok) or could not run an action: a toast. */
void notif_apps_action_done(uint32_t nid, bool ok);

#ifdef __cplusplus
}
#endif
