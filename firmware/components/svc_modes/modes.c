#include "modes.h"

#include <stddef.h>

static bool day_set(uint8_t days, int wday)
{
    return (days >> (((wday % 7) + 7) % 7)) & 1u;
}

bool mode_sched_active(const mode_sched_t *s, int wday, int min)
{
    if (!s->days || wday < 0) {
        return false;
    }
    const bool overnight = s->end_min <= s->start_min; // includes 24 h
    // Today's window
    if (day_set(s->days, wday) && min >= s->start_min && (overnight || min < s->end_min)) {
        return true;
    }
    // Yesterday's window running past midnight
    return overnight && day_set(s->days, wday + 6) && min < s->end_min;
}

uint32_t mode_sched_next_edge(const mode_sched_t *s, int wday, int min)
{
    if (!s->days || wday < 0) {
        return 0;
    }
    // A week of minutes, checked only when the time or a setting changes: cheap enough
    // and exact for every case (overnight, 24 h, back-to-back days).
    const bool now = mode_sched_active(s, wday, min);
    int d = wday;
    int m = min;
    for (uint32_t k = 1; k <= MODES_MIN_PER_WEEK; k++) {
        if (++m == MODES_MIN_PER_DAY) {
            m = 0;
            d = (d + 1) % 7;
        }
        if (mode_sched_active(s, d, m) != now) {
            return k;
        }
    }
    return 0;
}

modes_state_t modes_eval(modes_t *m, int wday, int min)
{
    bool on[MODE_COUNT];
    for (int i = 0; i < MODE_COUNT; i++) {
        const bool window = i != MODE_THEATER && mode_sched_active(&m->sched[i], wday, min);
        if (!window) {
            m->skip[i] = false;
        }
        on[i] = m->manual[i] || (window && !m->skip[i]);
    }
    return (modes_state_t){
        .dnd = on[MODE_DND],
        .sleep = on[MODE_SLEEP],
        .theater = on[MODE_THEATER],
        .quiet = on[MODE_DND] || on[MODE_SLEEP] || on[MODE_THEATER],
        .dark = on[MODE_SLEEP] || on[MODE_THEATER],
    };
}

void modes_set(modes_t *m, mode_id_t id, bool on, int wday, int min)
{
    if ((unsigned)id >= MODE_COUNT) {
        return;
    }
    m->manual[id] = on;
    m->skip[id] = !on && id != MODE_THEATER && mode_sched_active(&m->sched[id], wday, min);
}

uint32_t modes_next_edge(const modes_t *m, int wday, int min)
{
    uint32_t best = 0;
    for (int i = 0; i < MODE_COUNT; i++) {
        if (i == MODE_THEATER) {
            continue;
        }
        const uint32_t e = mode_sched_next_edge(&m->sched[i], wday, min);
        if (e && (!best || e < best)) {
            best = e;
        }
    }
    return best;
}

const char *mode_name(mode_id_t id)
{
    static const char *const k_names[] = {[MODE_DND] = "dnd", [MODE_SLEEP] = "sleep", [MODE_THEATER] = "theater"};
    return (unsigned)id < MODE_COUNT ? k_names[id] : "?";
}
