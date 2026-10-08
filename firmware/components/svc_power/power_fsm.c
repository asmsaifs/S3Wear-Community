#include "power_fsm.h"

#include <stddef.h>

static bool off_state(power_state_t s)
{
    return s == POWER_STATE_AOD || s == POWER_STATE_SLEEP || s == POWER_STATE_SAVER;
}

// Elapsed time since the last activity at which ACTIVE turns DIM (no DIM if the
// timeout is not longer than POWER_DIM_MS).
static uint32_t dim_at(const power_fsm_t *f)
{
    return f->policy.timeout_ms > POWER_DIM_MS ? f->policy.timeout_ms - POWER_DIM_MS : f->policy.timeout_ms;
}

static bool set_state(power_fsm_t *f, power_state_t s)
{
    if (f->state == s) {
        return false;
    }
    f->state = s;
    return true;
}

power_state_t power_fsm_off_state(const power_policy_t *p)
{
    return p->saver ? POWER_STATE_SAVER : p->aod ? POWER_STATE_AOD : POWER_STATE_SLEEP;
}

void power_fsm_init(power_fsm_t *f, const power_policy_t *p, uint32_t now)
{
    *f = (power_fsm_t){.policy = *p, .state = POWER_STATE_ACTIVE, .last_activity_ms = now};
}

bool power_fsm_activity(power_fsm_t *f, uint32_t now)
{
    if (f->state == POWER_STATE_WATCH_ONLY || f->state == POWER_STATE_OFF) {
        return false;
    }
    f->last_activity_ms = now;
    return set_state(f, POWER_STATE_ACTIVE);
}

bool power_fsm_screen_off(power_fsm_t *f, uint32_t now)
{
    (void)now;
    if (!power_state_screen_on(f->state)) {
        return false;
    }
    return set_state(f, power_fsm_off_state(&f->policy));
}

bool power_fsm_tick(power_fsm_t *f, uint32_t now)
{
    if (!power_state_screen_on(f->state) || f->holds) {
        return false;
    }
    const uint32_t elapsed = now - f->last_activity_ms;
    if (elapsed >= f->policy.timeout_ms) {
        return set_state(f, power_fsm_off_state(&f->policy));
    }
    if (elapsed >= dim_at(f)) {
        return set_state(f, POWER_STATE_DIM);
    }
    return false;
}

uint32_t power_fsm_ms_to_next(const power_fsm_t *f, uint32_t now)
{
    if (!power_state_screen_on(f->state) || f->holds) {
        return POWER_FSM_NO_DEADLINE;
    }
    const uint32_t elapsed = now - f->last_activity_ms;
    const uint32_t at = f->state == POWER_STATE_ACTIVE ? dim_at(f) : f->policy.timeout_ms;
    return elapsed >= at ? 0 : at - elapsed;
}

void power_fsm_hold(power_fsm_t *f, bool hold, uint32_t now)
{
    if (hold) {
        f->holds++;
    } else if (f->holds > 0 && --f->holds == 0) {
        f->last_activity_ms = now;
    }
}

bool power_fsm_set_policy(power_fsm_t *f, const power_policy_t *p)
{
    f->policy = *p;
    if (off_state(f->state)) {
        return set_state(f, power_fsm_off_state(p));
    }
    return false;
}

bool power_fsm_battery(power_fsm_t *f, int percent, bool vbus)
{
    if (percent < 0 || percent > POWER_CRITICAL_PCT || vbus) {
        f->critical_reads = 0;
        return false;
    }
    if (f->critical_reads < POWER_CRITICAL_READS) {
        f->critical_reads++;
    }
    if (f->critical_reads < POWER_CRITICAL_READS || f->state == POWER_STATE_OFF) {
        return false;
    }
    return set_state(f, POWER_STATE_WATCH_ONLY);
}

bool power_fsm_enter(power_fsm_t *f, power_state_t state)
{
    if (state != POWER_STATE_WATCH_ONLY && state != POWER_STATE_OFF) {
        return false;
    }
    return set_state(f, state);
}

bool power_state_screen_on(power_state_t s)
{
    return s == POWER_STATE_ACTIVE || s == POWER_STATE_DIM;
}

bool power_state_panel_on(power_state_t s)
{
    return power_state_screen_on(s) || s == POWER_STATE_AOD;
}

const char *power_state_name(power_state_t s)
{
    static const char *const k_names[] = {
        [POWER_STATE_ACTIVE] = "ACTIVE", [POWER_STATE_DIM] = "DIM",
        [POWER_STATE_AOD] = "AOD",       [POWER_STATE_SLEEP] = "SLEEP",
        [POWER_STATE_SAVER] = "SAVER",   [POWER_STATE_WATCH_ONLY] = "WATCH-ONLY",
        [POWER_STATE_OFF] = "OFF",
    };
    return (unsigned)s < POWER_STATE_COUNT ? k_names[s] : "?";
}
