// svc_power state machine (docs/02-firmware-architecture.md §7). Pure logic, no
// ESP-IDF: built by firmware/host_test. Time is a free-running millisecond counter;
// wrap-around is handled. svc_power.c owns the instance and applies each state.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POWER_STATE_ACTIVE = 0, // screen on, user brightness
    POWER_STATE_DIM,        // screen on, dimmed; the last POWER_DIM_MS before the timeout
    POWER_STATE_AOD,        // low-power face, panel on at AOD brightness
    POWER_STATE_SLEEP,      // panel asleep, touch in monitor mode, auto light sleep
    POWER_STATE_SAVER,      // as SLEEP with battery saver on (no AOD)
    POWER_STATE_WATCH_ONLY, // deep sleep, PWR key wakes (reboot)
    POWER_STATE_OFF,        // PMU off
    POWER_STATE_COUNT,
} power_state_t;

/** DIM starts this long before the screen-off timeout. */
#define POWER_DIM_MS 3000u
/** Battery at or below this %, without USB power, for POWER_CRITICAL_READS reads -> WATCH_ONLY. */
#define POWER_CRITICAL_PCT   3
#define POWER_CRITICAL_READS 2
/** power_fsm_ms_to_next() when no deadline is pending. */
#define POWER_FSM_NO_DEADLINE UINT32_MAX

typedef struct {
    uint32_t timeout_ms; // inactivity -> screen off
    bool aod;            // screen off goes to AOD instead of SLEEP
    bool saver;          // screen off goes to SAVER (no AOD)
} power_policy_t;

typedef struct {
    power_policy_t policy;
    power_state_t state;
    uint32_t last_activity_ms;
    uint16_t holds;          // "keep screen on" holders
    uint8_t critical_reads;  // consecutive critical battery reads
} power_fsm_t;

/** Starts in ACTIVE with the inactivity timer running from now. */
void power_fsm_init(power_fsm_t *f, const power_policy_t *p, uint32_t now);

/** Touch or button: restarts the timer; DIM, AOD, SLEEP, SAVER -> ACTIVE. True if the state changed. */
bool power_fsm_activity(power_fsm_t *f, uint32_t now);

/** Explicit screen off (PWR on the home screen): ACTIVE/DIM -> AOD/SLEEP/SAVER, even while held. */
bool power_fsm_screen_off(power_fsm_t *f, uint32_t now);

/** Evaluate the timeout at now: ACTIVE -> DIM -> screen off. True if the state changed. */
bool power_fsm_tick(power_fsm_t *f, uint32_t now);

/** Milliseconds until power_fsm_tick() has something to do (0 = overdue). */
uint32_t power_fsm_ms_to_next(const power_fsm_t *f, uint32_t now);

/** Keep the screen on (holders are counted). The last release restarts the timer. */
void power_fsm_hold(power_fsm_t *f, bool hold, uint32_t now);

/** New policy. While the screen is off, AOD/SLEEP/SAVER follow it. True if the state changed. */
bool power_fsm_set_policy(power_fsm_t *f, const power_policy_t *p);

/** Battery reading (percent < 0 = no battery). Repeated critical reads -> WATCH_ONLY. */
bool power_fsm_battery(power_fsm_t *f, int percent, bool vbus);

/** Explicit WATCH_ONLY or OFF (power menu); other states are refused. True if the state changed. */
bool power_fsm_enter(power_fsm_t *f, power_state_t state);

/** The screen-off state the policy selects. */
power_state_t power_fsm_off_state(const power_policy_t *p);

/** ACTIVE or DIM: the user is looking at the watch. */
bool power_state_screen_on(power_state_t s);
/** ACTIVE, DIM or AOD: the panel shows something. */
bool power_state_panel_on(power_state_t s);
const char *power_state_name(power_state_t s);

#ifdef __cplusplus
}
#endif
