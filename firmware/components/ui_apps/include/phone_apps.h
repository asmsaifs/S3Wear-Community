// Phone link UI (docs/04-ui-ux.md §4f, P4-01): the pairing code alert. Portable
// (LVGL + ui_framework): the simulator builds it too. UI task only, like ui_nav.h.
//
// The app shows a pairing request from svc_ble with phone_apps_pair_request() and
// reports the end of it (any outcome) with phone_apps_pair_done().
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The user's answer: the code matches the phone's (accept) or not. Called once. */
typedef void (*phone_pair_reply_t)(bool accept, void *ctx);

/**
 * Full-screen alert with the 6-digit code ("123 456"), Pair / Cancel; BACK cancels.
 * replaces: another phone is paired and confirming replaces it. A new request
 * replaces an unanswered one (its reply is not called).
 */
void phone_apps_pair_request(uint32_t code, bool replaces, phone_pair_reply_t reply, void *ctx);

/** Pairing ended: closes the alert if still open; toast "Phone paired" or, unless the
 *  user cancelled it, "Pairing failed". */
void phone_apps_pair_done(bool ok);

#ifdef __cplusplus
}
#endif
