// Clock apps (docs/03-firmware-features.md F6, docs/04-ui-ux.md §4c, P3-07): Alarms
// (list "alarms", edit "alarms.edit"), the ring screen ("ring"), Timer ("timer",
// "timer.custom"), Stopwatch ("stopwatch") and World clock ("world_clock",
// "world_clock.add"). Portable (LVGL + ui_framework + watchfaces + the pure alarm and
// timer model): the simulator builds it too. UI task only, like ui_nav.h.
//
// The screens call no service: alarms, timers and ringing go through a backend
// (app_main: svc_alarm; simulator: in memory over the same alarm_sched/timer_set).
// The backend reports back with clock_apps_changed(), clock_apps_ring() and
// clock_apps_ring_end(). The stopwatch lives in the UI (LVGL tick) and keeps running
// while its screen is closed.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "alarm_sched.h"
#include "esp_err.h"
#include "timer_set.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CLOCK_TIMER_PAUSE,
    CLOCK_TIMER_RESUME,
    CLOCK_TIMER_RESTART,
    CLOCK_TIMER_REMOVE,
} clock_timer_action_t;

typedef struct {
    /** Copy of the alarms and snooze state. */
    void (*alarms)(alarm_set_t *out, void *ctx);
    /** Add (id 0) or replace an alarm: ESP_OK, ESP_ERR_NO_MEM (ALARM_MAX), or another error. */
    esp_err_t (*alarm_put)(const alarm_t *a, void *ctx);
    void (*alarm_delete)(uint8_t id, void *ctx);
    /** Next alarm or snooze, UTC s; 0 = none (or the time is unknown). */
    int64_t (*alarm_next)(void *ctx);
    /** Copy of the timers and the millisecond clock they run on. */
    void (*timers)(timer_set_t *out, uint32_t *now_ms, void *ctx);
    /** ESP_OK, ESP_ERR_NO_MEM (TIMER_MAX) or another error. */
    esp_err_t (*timer_start)(uint32_t duration_ms, void *ctx);
    void (*timer_action)(uint8_t id, clock_timer_action_t action, void *ctx);
    /** The ring screen's buttons (a timer: snooze = stop). */
    void (*ring_snooze)(void *ctx);
    void (*ring_dismiss)(void *ctx);
    void *ctx;
} clock_backend_t;

/** Register the screens (shell_init() does this). */
void clock_apps_init(void);

/** Install the backend (copied). Without one the apps show empty lists. */
void clock_apps_set_backend(const clock_backend_t *backend);

/** Alarms or timers changed: refresh the visible clock screen and the face data
 *  (next alarm, timer end), on the next LVGL timer run (so it may be called from a
 *  backend callback inside a click). */
void clock_apps_changed(void);

// --- Ringing -------------------------------------------------------------------------------

typedef enum {
    CLOCK_RING_ALARM,
    CLOCK_RING_TIMER,
} clock_ring_kind_t;

typedef struct {
    uint8_t kind;         // clock_ring_kind_t
    uint8_t id;
    uint8_t snooze_min;   // alarm
    int64_t at;           // alarm: UTC s of the occurrence (shown as the time)
    uint32_t duration_ms; // timer
    char label[ALARM_LABEL_MAX + 1];
} clock_ring_t;

/** Show the ring screen above everything (full screen, keeps the screen on; BACK and
 *  edge swipes do nothing). The same ring again does nothing. */
void clock_apps_ring(const clock_ring_t *ring);
/** The ringing stopped (from the backend): close the ring screen if it is still open;
 *  "Snoozed for N min" when snoozed. */
void clock_apps_ring_end(bool snoozed, uint8_t snooze_min);
bool clock_apps_ring_active(void);
/** PWR while ringing: snooze an alarm, stop a timer. */
void clock_apps_ring_key(void);

// --- World clock -------------------------------------------------------------------------

/** Cities to show (WORLD_CLOCKS setting, world_clock.h). The first one also feeds the
 *  world time complication. */
void clock_apps_set_world(const char *csv);
/** cb runs when the user added or removed a city (app_main saves WORLD_CLOCKS). */
void clock_apps_set_world_listener(void (*cb)(const char *csv, void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif
