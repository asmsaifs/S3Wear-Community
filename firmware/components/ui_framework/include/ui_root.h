// Portable UI entry points: built for the watch and for the simulator, using only
// LVGL and the HAL. Call with LVGL locked (UI task, or lv_lock() held).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Pointer input device reading hal_touch_read() (gestures and long-press come from LVGL). */
lv_indev_t *ui_pointer_create(lv_display_t *disp);

/** Screen off/AOD: stop polling the pointer (no periodic wake-up). Enabling again
 *  ignores the touch that woke the screen until the finger is lifted. */
void ui_pointer_set_enabled(bool enabled);

/** Boot logo on the active screen; the first thing drawn after reset. */
void ui_boot_screen_show(void);

/** WATCH-ONLY screen (docs/03 F5): the time in light digits and the battery %, little
 *  else lit. Loaded as a new active screen (the previous one is kept). */
typedef struct {
    time_t at;          // the minute to show (UTC); 0 = ui_clock_now(). 12/24 h and
                        // "time unknown" follow ui_clock_set_24h()/_set_valid()
    int8_t dx, dy;      // burn-in shift (wf_aod_shift)
    int8_t battery_pct; // -1 = not shown
} ui_watch_only_args_t;
void ui_watch_only_show(const ui_watch_only_args_t *args);

#ifdef __cplusplus
}
#endif
