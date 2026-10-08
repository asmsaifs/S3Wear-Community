// Calls: the incoming-call screen, the call screen and the Calls app with the missed calls
// (docs/03 F8, docs/04 §4k, P6-04). Portable (LVGL + ui_framework): the simulator builds it
// too. UI task only, like ui_nav.h.
//
// The screens call no service: commands to the phone go through a backend (app_main: svc_call;
// simulator: sim_call.c). The calls come in with call_apps_set(), with what changed; failed
// commands with call_apps_result().
#pragma once

#include <stdbool.h>
#include "call_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Why a command failed (shown as a toast). */
typedef enum {
    CALL_RESULT_OK,
    CALL_RESULT_OFFLINE,     // "Phone not connected"
    CALL_RESULT_NO_ANSWER,   // "Phone did not answer"
    CALL_RESULT_UNSUPPORTED, // "Update the phone app"
    CALL_RESULT_DENIED,      // "Allow call control on your phone"
    CALL_RESULT_REFUSED,     // "Phone could not do that"
} call_result_t;

typedef struct {
    void (*command)(call_cmd_t cmd, void *ctx); // ask the phone; a failure comes back as call_apps_result()
    void (*silence)(void *ctx);                 // stop the watch ringing only (the incoming screen was closed)
    void (*missed_clear)(void *ctx);            // forget the missed calls; the new list comes back with call_apps_set()
    void *ctx;
} call_backend_t;

/** Register the screens (shell_init() does this). */
void call_apps_init(void);

/** Install the backend (copied). Without one, commands say "Phone not connected". */
void call_apps_set_backend(const call_backend_t *backend);

/**
 * The calls changed (copied). act says how: RING shows the incoming-call screen above everything
 * (clearing overlays), ANSWERED turns it into the call screen, ENDED / MISSED close them (MISSED
 * also shows a "Missed call" banner), UPDATE / ACTIVE / NONE refresh what is open.
 */
void call_apps_set(const calls_t *c, call_action_t act);

/** A command failed: a toast says why. */
void call_apps_result(call_cmd_t cmd, call_result_t result);

/** The incoming-call screen is up. */
bool call_apps_ringing(void);

/** PWR while the incoming-call screen is up: mute the ring (watch and phone). False if it was muted already. */
bool call_apps_key(void);

#ifdef __cplusplus
}
#endif
