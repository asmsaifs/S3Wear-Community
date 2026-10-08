#include "battery_hist.h"

#include <string.h>

void battery_hist_init(battery_hist_t *h)
{
    memset(h, 0, sizeof *h);
    memset(h->slot, BATTERY_HIST_NONE, sizeof h->slot);
}

// Move the ring to the slot holding now_s; skipped slots have no reading.
static void advance(battery_hist_t *h, uint32_t now_s)
{
    if (!h->started) {
        h->started = true;
        h->head_start_s = now_s;
        return;
    }
    uint32_t steps = (now_s - h->head_start_s) / BATTERY_HIST_SLOT_S;
    if (steps == 0) {
        return;
    }
    h->head_start_s += steps * BATTERY_HIST_SLOT_S;
    if (steps >= BATTERY_HIST_SLOTS) {
        memset(h->slot, BATTERY_HIST_NONE, sizeof h->slot);
        steps %= BATTERY_HIST_SLOTS;
    }
    while (steps--) {
        h->head = (uint8_t)((h->head + 1) % BATTERY_HIST_SLOTS);
        h->slot[h->head] = BATTERY_HIST_NONE;
    }
}

void battery_hist_add(battery_hist_t *h, uint32_t now_s, int percent, bool charging)
{
    advance(h, now_s);
    if (percent < 0 || percent > 100) {
        h->slot[h->head] = BATTERY_HIST_NONE;
        h->anchor_valid = false;
        return;
    }
    h->slot[h->head] = (uint8_t)(percent | (charging ? BATTERY_HIST_CHARGING : 0));

    // A new direction, or the gauge moving against it (recalibration), restarts the rate.
    const bool against = charging ? percent < h->anchor_pct : percent > h->anchor_pct;
    if (!h->anchor_valid || charging != h->anchor_charging || against) {
        h->anchor_valid = true;
        h->anchor_charging = charging;
        h->anchor_pct = (int8_t)percent;
        h->anchor_s = now_s;
    }
}

void battery_hist_get(const battery_hist_t *h, uint32_t now_s, uint8_t out[BATTERY_HIST_SLOTS])
{
    battery_hist_t c = *h;
    if (c.started) {
        advance(&c, now_s);
    }
    for (int i = 0; i < BATTERY_HIST_SLOTS; i++) {
        out[i] = c.slot[(c.head + 1 + i) % BATTERY_HIST_SLOTS];
    }
}

int32_t battery_hist_minutes(const battery_hist_t *h, uint32_t now_s, int percent, bool charging)
{
    if (percent < 0 || percent > 100 || !h->anchor_valid || charging != h->anchor_charging) {
        return -1;
    }
    if (charging && percent >= 100) {
        return 0;
    }
    const int moved = charging ? percent - h->anchor_pct : h->anchor_pct - percent;
    const uint32_t elapsed = now_s - h->anchor_s;
    if (moved < BATTERY_EST_MIN_STEP || elapsed < BATTERY_EST_MIN_S) {
        return -1;
    }
    int remaining = percent; // % to drain
    if (charging) {
        const int cv_from = percent > BATTERY_EST_CV_PCT ? percent : BATTERY_EST_CV_PCT;
        remaining = (100 - percent) + (100 - cv_from);
    }
    return (int32_t)((uint64_t)remaining * elapsed / (uint32_t)moved / 60u);
}
