// Battery screens and low-battery flows (docs/03-firmware-features.md F5, docs/04-ui-ux.md
// §4d, P3-09): the Battery app ("battery"), the charging screen ("charging") and the
// low-battery toast and alerts. Portable (LVGL + ui_framework + the pure battery
// history): the simulator builds it too. UI task only, like ui_nav.h.
//
// The screens call no service: readings, saver and watch-only go through a backend
// (app_main: svc_power; simulator: hal_sim's fake battery). The app reports battery
// events with battery_apps_changed(), battery_apps_charger(), battery_apps_screen_on()
// and battery_apps_low().
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "battery_hist.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int8_t percent; // -1 = no battery / unknown
    uint16_t mv;
    bool charging;
    bool vbus;
    bool saver;
    int32_t minutes;                     // to full (charging) or left, -1 = unknown
    uint8_t history[BATTERY_HIST_SLOTS]; // battery_hist_get(): oldest first, 15 min slots
} battery_info_t;

typedef struct {
    void (*read)(battery_info_t *out, void *ctx);
    void (*set_saver)(bool on, void *ctx);
    void (*watch_only)(void *ctx); // enter WATCH-ONLY (already confirmed)
    void *ctx;
} battery_backend_t;

/** Register the screens (shell_init() does this). */
void battery_apps_init(void);

/** Install the backend (copied). Without one the screens show "--". */
void battery_apps_set_backend(const battery_backend_t *backend);

/** A new reading or saver change: refresh the open battery or charging screen. */
void battery_apps_changed(void);

/** USB power came (show the charging screen) or went (close it). */
void battery_apps_charger(bool vbus);

/** The screen just woke: on USB power with the face on top, show the charging screen. */
void battery_apps_screen_on(void);

/** Low battery (SVC_POWER_EVT_BATTERY_LOW): 15 % toast, 10 % offer saver, 3 % watch-only alert. */
void battery_apps_low(uint8_t threshold, int8_t percent);

/** "3 h 20 min", "1 d 4 h", "45 min" (minutes >= 0). */
void battery_fmt_minutes(int32_t minutes, char *buf, size_t len);

#ifdef __cplusplus
}
#endif
