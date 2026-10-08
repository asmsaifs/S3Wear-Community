#include "button_gesture.h"

#include <string.h>

static void reset(btn_gesture_fsm_t *f)
{
    f->clicks = 0;
    f->long_sent = false;
}

static btn_gesture_t clicks_gesture(uint8_t clicks)
{
    switch (clicks) {
    case 1:
        return BTN_GESTURE_SHORT;
    case 2:
        return BTN_GESTURE_DOUBLE;
    case 3:
        return BTN_GESTURE_TRIPLE;
    default:
        return BTN_GESTURE_NONE;
    }
}

static bool long_pending(const btn_gesture_fsm_t *f)
{
    return f->down && !f->long_sent && f->clicks == 0 && f->cfg.long_ms > 0;
}

void btn_gesture_set_cfg(btn_gesture_fsm_t *f, const btn_gesture_cfg_t *cfg)
{
    f->cfg = *cfg;
    if (f->cfg.max_clicks < 1) {
        f->cfg.max_clicks = 1;
    } else if (f->cfg.max_clicks > BTN_GESTURE_MAX_CLICKS) {
        f->cfg.max_clicks = BTN_GESTURE_MAX_CLICKS;
    }
    reset(f);
    f->long_sent = f->down; // a press in progress reports nothing
}

void btn_gesture_init(btn_gesture_fsm_t *f, const btn_gesture_cfg_t *cfg)
{
    memset(f, 0, sizeof *f);
    btn_gesture_set_cfg(f, cfg);
}

void btn_gesture_cancel(btn_gesture_fsm_t *f)
{
    reset(f);
    f->long_sent = f->down;
}

btn_gesture_t btn_gesture_press(btn_gesture_fsm_t *f, uint32_t now)
{
    // A press after the gap (tick not run yet) ends the previous gesture first.
    const btn_gesture_t done = btn_gesture_tick(f, now);
    f->down = true;
    f->t_down = now;
    return done;
}

btn_gesture_t btn_gesture_release(btn_gesture_fsm_t *f, uint32_t now)
{
    if (!f->down) {
        return BTN_GESTURE_NONE;
    }
    btn_gesture_t g = btn_gesture_tick(f, now); // LONG reached before the release was seen
    f->down = false;
    if (f->long_sent) {
        reset(f);
        return g;
    }
    f->clicks++;
    f->t_up = now;
    if (f->clicks >= f->cfg.max_clicks) {
        g = clicks_gesture(f->clicks);
        reset(f);
    }
    return g;
}

btn_gesture_t btn_gesture_tick(btn_gesture_fsm_t *f, uint32_t now)
{
    if (long_pending(f) && now - f->t_down >= f->cfg.long_ms) {
        f->long_sent = true;
        return BTN_GESTURE_LONG;
    }
    if (!f->down && f->clicks > 0 && now - f->t_up >= f->cfg.gap_ms) {
        const btn_gesture_t g = clicks_gesture(f->clicks);
        reset(f);
        return g;
    }
    return BTN_GESTURE_NONE;
}

uint32_t btn_gesture_ms_to_next(const btn_gesture_fsm_t *f, uint32_t now)
{
    if (long_pending(f)) {
        const uint32_t held = now - f->t_down;
        return held >= f->cfg.long_ms ? 0 : f->cfg.long_ms - held;
    }
    if (!f->down && f->clicks > 0) {
        const uint32_t since = now - f->t_up;
        return since >= f->cfg.gap_ms ? 0 : f->cfg.gap_ms - since;
    }
    return BTN_GESTURE_NO_DEADLINE;
}

const char *btn_gesture_name(btn_gesture_t g)
{
    static const char *const k_names[] = {
        [BTN_GESTURE_NONE] = "none",     [BTN_GESTURE_SHORT] = "short", [BTN_GESTURE_DOUBLE] = "double",
        [BTN_GESTURE_TRIPLE] = "triple", [BTN_GESTURE_LONG] = "long",
    };
    return (unsigned)g < BTN_GESTURE_COUNT ? k_names[g] : "?";
}
