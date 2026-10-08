/* Simulator HAL (firmware/components/s3w_hal/include) over SDL2 and the host clock.
 *
 *   mouse            touch (left button = finger down)
 *   Esc / Backspace  BACK button
 *   P                POWER button
 *   C                plug / unplug the charger
 *
 * Single-threaded: callbacks run from hal_sim_poll() in the main loop. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hal_input.h"

/* headless: no window; touch and buttons come only from the *_inject() calls. */
void hal_sim_init(bool headless);

/* Main loop, once per iteration: keyboard -> buttons/charger, RTC alarm. */
void hal_sim_poll(void);

/* Scripted input (headless runs, see sim_script.c). */
void hal_sim_touch_inject(bool pressed, uint16_t x, uint16_t y);
void hal_sim_button_inject(hal_button_t button, bool pressed);

/* Fake battery level and USB power (charging below 100 %); a USB change posts the
 * PMU events, as the C key does. */
void hal_sim_battery_set(int percent, bool vbus);
