// Button press recognizer for svc_input (docs/02-firmware-architecture.md §7):
// short, double, triple and long presses from debounced press/release edges. Pure
// logic, no ESP-IDF: built by firmware/host_test. Time is a free-running
// millisecond counter; wrap-around is handled.
//
// A click is a press released before long_ms. Clicks less than gap_ms apart form one
// gesture, reported gap_ms after the last release, or at once on the release that
// reaches max_clicks (max_clicks = 1: a short press is reported on release with no
// wait). A press held long_ms as the first of a sequence is LONG, reported while
// still held; its release reports nothing.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTN_GESTURE_NONE = 0,
    BTN_GESTURE_SHORT,
    BTN_GESTURE_DOUBLE,
    BTN_GESTURE_TRIPLE,
    BTN_GESTURE_LONG,
    BTN_GESTURE_COUNT,
} btn_gesture_t;

/** btn_gesture_ms_to_next() when no deadline is pending. */
#define BTN_GESTURE_NO_DEADLINE UINT32_MAX
/** Most clicks one gesture can have (TRIPLE). */
#define BTN_GESTURE_MAX_CLICKS 3

typedef struct {
    uint32_t long_ms;   // held this long -> LONG (0: no long press)
    uint32_t gap_ms;    // next press within this after a release continues the gesture
    uint8_t max_clicks; // 1..BTN_GESTURE_MAX_CLICKS
} btn_gesture_cfg_t;

typedef struct {
    btn_gesture_cfg_t cfg;
    bool down;
    bool long_sent; // this press already reported LONG
    uint8_t clicks; // completed clicks in the current gesture
    uint32_t t_down;
    uint32_t t_up;
} btn_gesture_fsm_t;

void btn_gesture_init(btn_gesture_fsm_t *f, const btn_gesture_cfg_t *cfg);
/** New config; a gesture in progress is dropped. */
void btn_gesture_set_cfg(btn_gesture_fsm_t *f, const btn_gesture_cfg_t *cfg);
/** Forget any gesture in progress (the next release reports nothing). */
void btn_gesture_cancel(btn_gesture_fsm_t *f);

btn_gesture_t btn_gesture_press(btn_gesture_fsm_t *f, uint32_t now);
btn_gesture_t btn_gesture_release(btn_gesture_fsm_t *f, uint32_t now);
/** Deadlines (LONG while held, end of the click gap). */
btn_gesture_t btn_gesture_tick(btn_gesture_fsm_t *f, uint32_t now);
/** Milliseconds until btn_gesture_tick() has something to do (0 = now). */
uint32_t btn_gesture_ms_to_next(const btn_gesture_fsm_t *f, uint32_t now);

const char *btn_gesture_name(btn_gesture_t g);

#ifdef __cplusplus
}
#endif
