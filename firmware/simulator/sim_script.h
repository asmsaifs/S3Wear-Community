/* Headless scenario scripts for UI snapshot tests (firmware/test/ui/README.md). */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Clock pinned for headless runs (ui_clock_set_source). Default 2026-10-03 10:09 UTC. */
time_t sim_fixed_now(void);

/* Advance LVGL's clock by ms in 5 ms steps, running timers, animations and input. */
void sim_step(uint32_t ms);

/* Run every command in path. Returns false (after printing the line) on any error. */
bool sim_script_run(const char *path, lv_display_t *disp);

#ifdef __cplusplus
}
#endif
