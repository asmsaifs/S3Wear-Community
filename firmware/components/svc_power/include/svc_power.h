// Power service (docs/02-firmware-architecture.md §7): screen states and timeout,
// wake sources, PM locks and automatic light sleep, battery events. Buttons and
// palm cover are svc_input (P2-07), which calls svc_power_wake()/_screen_off().
//
// One task (svc_power, core 0, prio 18) owns the state machine (power_fsm.h). Every
// call here only queues a request and returns at once, so it is safe from any task
// including the UI task. Results arrive as SVC_POWER_EVENT events.
//
// Screen on (ACTIVE/DIM): CPU at full speed, no light sleep. Screen off: the panel
// sleeps, touch goes to monitor mode, the chosen wake inputs are armed and the
// CPU light-sleeps whenever all tasks are idle.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "battery_hist.h"
#include "esp_err.h"
#include "power_fsm.h"
#include "svc_power_events.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What the UI must do for a screen state. */
typedef enum {
    SVC_POWER_UI_OFF, // panel about to sleep: stop drawing, pause screens and input
    SVC_POWER_UI_AOD, // low-power face: home, minute updates only, no input
    SVC_POWER_UI_ON,  // interactive: resume, take input, draw a fresh frame now
    SVC_POWER_UI_WATCH_ONLY, // the time-only screen, drawn now; deep sleep follows (panel stays on)
} svc_power_ui_mode_t;

/**
 * Runs on the UI task (posted by svc_power, which waits for it to return before it
 * touches the panel). Registered by the app before svc_power_start(); without it
 * the panel is still switched but nothing is drawn.
 */
typedef void (*svc_power_ui_hook_t)(svc_power_ui_mode_t mode);
void svc_power_set_ui_hook(svc_power_ui_hook_t hook);

/**
 * After the display, settings and the event bus: configures DFS + automatic light
 * sleep, takes the touch-down and PMU callbacks, applies the display
 * settings and starts in ACTIVE.
 */
esp_err_t svc_power_start(void);

typedef enum {
    SVC_POWER_WAKE_TOUCH,
    SVC_POWER_WAKE_BUTTON,
    SVC_POWER_WAKE_CHARGER,
    SVC_POWER_WAKE_ALARM,   // for svc_alarm (P3-07)
    SVC_POWER_WAKE_NOTIFY,  // for svc_notify; callers check WAKE_ON_NOTIFY themselves
    SVC_POWER_WAKE_RAISE,   // for svc_sensors (P3-05)
    SVC_POWER_WAKE_CONSOLE,
    SVC_POWER_WAKE_BATTERY, // low battery 10 % / 3 % (not in DND, sleep or theater mode)
    SVC_POWER_WAKE_PAIRING, // a phone asks to pair: the code must be seen (every mode)
    SVC_POWER_WAKE_FIND,    // the phone is looking for the watch (svc_find; every mode)
    SVC_POWER_WAKE_CALL,    // an incoming call (svc_call; not in DND, sleep or theater mode)
    SVC_POWER_WAKE_COUNT,
} svc_power_wake_t;

/** Screen on (ACTIVE) and restart the timeout. */
esp_err_t svc_power_wake(svc_power_wake_t reason);
/** User input while the screen is on: restart the timeout. */
esp_err_t svc_power_user_activity(void);
/** Screen off now (PWR on the home screen, palm cover). */
esp_err_t svc_power_screen_off(void);
/** Keep the screen on while held (counted; UI_SCREEN_KEEP_ON, workouts). */
esp_err_t svc_power_hold_screen(bool hold);
/** Full panel brightness while on (flashlight); off returns to the DISPLAY_BRIGHTNESS setting,
 *  which is never changed. Not counted: one user at a time. */
esp_err_t svc_power_boost_brightness(bool on);
/** Battery saver on/off (persisted as the BATTERY_SAVER setting). */
esp_err_t svc_power_set_saver(bool on);
/** WATCH-ONLY: the time-only screen, then deep sleep woken once a minute to redraw
 *  it, by the next alarm or by PWR (waking for good is a reboot). */
esp_err_t svc_power_enter_watch_only(void);
/** Next alarm (UTC s, 0 = none) for the WATCH-ONLY deep-sleep timer (svc_alarm). */
void svc_power_set_alarm_wake(int64_t utc);
/** True after a boot by that timer until the user wakes the screen (button, touch,
 *  charger): the screen stays off until the alarm, and svc_alarm returns to
 *  WATCH-ONLY once it is dismissed. */
bool svc_power_alarm_boot(void);
/** What this boot is after a WATCH-ONLY deep sleep (app_main asks first thing). */
typedef enum {
    SVC_POWER_BOOT_NORMAL,     // not from WATCH-ONLY, or by PWR
    SVC_POWER_BOOT_WATCH_TICK, // the minute timer: redraw the time, then svc_power_watch_only_sleep()
    SVC_POWER_BOOT_ALARM,      // the alarm is near: boot fully, screen off (svc_power_alarm_boot())
} svc_power_boot_t;
svc_power_boot_t svc_power_boot_kind(void);
/** Minute tick done (the panel shows the new time): deep sleep again. dark: battery
 *  empty, panel off and no more minute ticks. Before svc_power_start(); does not return. */
void svc_power_watch_only_sleep(bool dark);
/** Leave WATCH-ONLY on this tick boot (USB power came back): the boot continues normally. */
void svc_power_watch_only_exit(void);

/** Panel off, then PMU off. PWR turns the watch back on. */
esp_err_t svc_power_shutdown(void);
/** Panel off, then esp_restart(). */
esp_err_t svc_power_restart(void);

power_state_t svc_power_state(void);
bool svc_power_saver(void);
/** Last battery reading (ESP_ERR_INVALID_STATE before the first one). */
esp_err_t svc_power_battery(svc_power_battery_t *out);

/** Battery screen data: the last reading, the estimate and the 24 h history. */
typedef struct {
    svc_power_battery_t battery;
    int32_t minutes;                       // to full (charging) or until empty, -1 = unknown
    uint8_t history[BATTERY_HIST_SLOTS];   // battery_hist_get(): oldest first, 15 min slots
} svc_power_battery_info_t;
esp_err_t svc_power_battery_info(svc_power_battery_info_t *out);

typedef struct {
    power_state_t state;
    bool saver;
    uint16_t holds;
    uint32_t timeout_ms;
    uint64_t state_ms[POWER_STATE_COUNT]; // time spent in each state since boot
    uint32_t wakes[SVC_POWER_WAKE_COUNT]; // screen wake-ups by reason
    uint32_t raise_unused;                // raise wakes that went off again untouched (likely false)
    uint32_t light_sleeps;                // automatic light-sleep entries (CONFIG_PM_LIGHT_SLEEP_CALLBACKS)
    uint64_t light_sleep_us;              // time in light sleep
    // Battery drain estimate since the last discharge segment start (unplugged or
    // a 1 % step): mA = %drop x capacity / time. 0 until the first step.
    int8_t seg_start_pct;
    uint32_t seg_ms;
    uint32_t last_drain_ma_x10;           // last completed 1 % step, tenths of mA
    uint32_t stack_free;                  // svc_power task stack high-water mark, bytes
} svc_power_stats_t;

void svc_power_get_stats(svc_power_stats_t *out);
const char *svc_power_wake_name(svc_power_wake_t reason);

#ifdef __cplusplus
}
#endif
