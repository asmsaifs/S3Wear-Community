// Weather app, and the weather data of the watch faces, complications and the Weather tile
// (docs/03 F10, docs/04 §4i, P6-02). Portable (LVGL + ui_framework + watchfaces): the simulator
// builds it too. UI task only, like ui_nav.h.
//
// The forecast comes in with weather_apps_set() (app_main: svc_weather events; simulator:
// sim_weather.c). From it, and again on every minute, the weather fields of wf_data (temperature,
// condition, today's low / high, sunrise / sunset, stale) are recomputed with weather_now(), so the
// faces follow the hourly list while the phone is away.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "weather_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Register the screen and the minute update (shell_init() does this). */
void weather_apps_init(void);

/** A new forecast (copied): update wf_data and the Weather app (id "weather") if open. */
void weather_apps_set(const weather_t *w);

/** The forecast as last set. */
const weather_t *weather_apps_get(void);

/** Icon for a condition (UTF-8, in the UI text fonts): sun / moon, clouds, rain, snow, bolt, fog. */
const char *weather_apps_symbol(uint8_t cond, bool day);

/** The icon's colour (0xRRGGBB). */
uint32_t weather_apps_color(uint8_t cond, bool day);

#ifdef __cplusplus
}
#endif
