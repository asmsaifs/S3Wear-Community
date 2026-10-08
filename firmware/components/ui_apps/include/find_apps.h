// Find phone app and find-watch screen (docs/03 F13, docs/04 §4g, P4-08). Portable (LVGL +
// ui_framework): the simulator builds it too. UI task only, like ui_nav.h.
//
// The screens call no service: ringing the phone and stopping the watch go through a backend
// (app_main: svc_find; simulator: sim_find.c). The app reports what happened with
// find_apps_phone() and find_apps_watch_ring().
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FIND_PHONE_IDLE,
    FIND_PHONE_ASKING,  // asked, no answer yet
    FIND_PHONE_RINGING, // the phone said it rings
} find_phone_state_t;

/** Why the phone state changed; anything but OK / STOPPED is shown under the button. */
typedef enum {
    FIND_RESULT_OK,
    FIND_RESULT_STOPPED,     // the phone stopped (stopped there, or its time ran out)
    FIND_RESULT_OFFLINE,     // "Phone not connected"
    FIND_RESULT_NO_ANSWER,   // "Phone did not answer"
    FIND_RESULT_UNSUPPORTED, // "Update the phone app"
    FIND_RESULT_REFUSED,     // "Phone could not ring"
} find_result_t;

typedef struct {
    void (*ring_phone)(bool ring, void *ctx); // ask the phone to ring / stop; the outcome comes as find_apps_phone()
    void (*stop_watch)(void *ctx);            // the user stopped the watch ringing
    void *ctx;
} find_backend_t;

/** Register the screens (shell_init() does this). */
void find_apps_init(void);

/** Install the backend (copied). Without one, ringing the phone says "Phone not connected". */
void find_apps_set_backend(const find_backend_t *backend);

/** The phone's ringing state changed: refresh the Find phone app (id "find_phone") if open. */
void find_apps_phone(find_phone_state_t state, find_result_t result);

/**
 * The phone started (true) or stopped (false) the watch ringing: show the flashing full-screen
 * "find_watch" screen above everything (clearing overlays), or close it.
 */
void find_apps_watch_ring(bool on);

/** The find-watch screen is up (PWR then stops it: find_apps_watch_key()). */
bool find_apps_watch_active(void);

/** PWR while the watch rings: stop it, as the Stop button does. */
void find_apps_watch_key(void);

#ifdef __cplusplus
}
#endif
