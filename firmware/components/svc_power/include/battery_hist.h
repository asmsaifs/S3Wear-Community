// Battery history and time estimates (docs/03-firmware-features.md F5). Pure logic, no
// ESP-IDF: built by firmware/host_test and the simulator. svc_power owns the instance
// and feeds it every battery reading; time is a monotonic second counter.
//
// History: the last 24 h in 96 slots of 15 min, each the last reading in that slot
// (percent, and whether it was charging). RAM only: a reboot starts it empty.
//
// Estimate: the average rate since an anchor, taken when charging starts or stops
// (or the gauge moves against the direction). It needs BATTERY_EST_MIN_STEP % and
// BATTERY_EST_MIN_S of data; until then it is unknown.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BATTERY_HIST_SLOTS    96
#define BATTERY_HIST_SLOT_S   900
/** Slot value: no reading in that slot. */
#define BATTERY_HIST_NONE     0xFFu
/** Slot value: percent | BATTERY_HIST_CHARGING while charging. */
#define BATTERY_HIST_CHARGING 0x80u
#define BATTERY_HIST_PCT(v)   ((int)((v) & 0x7Fu))

#define BATTERY_EST_MIN_STEP 2   // % moved since the anchor
#define BATTERY_EST_MIN_S    600 // and this much time
/** Charging slows down above this % (constant voltage phase): the % above it count double. */
#define BATTERY_EST_CV_PCT   80

typedef struct {
    uint8_t slot[BATTERY_HIST_SLOTS]; // ring; slot[head] is the current slot
    uint8_t head;
    bool started;
    uint32_t head_start_s;
    bool anchor_valid;
    bool anchor_charging;
    int8_t anchor_pct;
    uint32_t anchor_s;
} battery_hist_t;

void battery_hist_init(battery_hist_t *h);

/** A battery reading (percent < 0 = no battery: no slot value, no estimate). */
void battery_hist_add(battery_hist_t *h, uint32_t now_s, int percent, bool charging);

/** The last 24 h, oldest first: out[BATTERY_HIST_SLOTS - 1] is the slot of now_s. */
void battery_hist_get(const battery_hist_t *h, uint32_t now_s, uint8_t out[BATTERY_HIST_SLOTS]);

/**
 * Minutes to full (charging) or until empty (discharging) at the current rate, -1 if
 * unknown. 0 when charging and at 100 %.
 */
int32_t battery_hist_minutes(const battery_hist_t *h, uint32_t now_s, int percent, bool charging);

#ifdef __cplusplus
}
#endif
