// ui_apps internals: the battery screens and what they share (battery_apps.h).
#pragma once

#include "battery_apps.h"
#include "shell_priv.h"

extern const screen_def_t battery_app_screen;
extern const screen_def_t battery_charging_screen;

/** The backend; every member is set (stubs when none was installed). */
const battery_backend_t *battery_backend(void);

/** Re-read the backend into an open screen (battery_apps_changed()). */
void battery_app_refresh(ui_screen_t *s);
void battery_charging_refresh(ui_screen_t *s);

/** Ring colour for a level: green, yellow at 20 % and below, red at 10 % and below. */
uint32_t battery_level_color(int percent);

/** "Charging", "Charged", "Not charging", "Battery saver on", "On battery". */
const char *battery_state_text(const battery_info_t *b);
/** "Full in about 1 h 20 min", "About 14 h left", "Estimating time left", ...; empty
 *  on USB power without charging. */
void battery_estimate_text(const battery_info_t *b, char *buf, size_t len);
