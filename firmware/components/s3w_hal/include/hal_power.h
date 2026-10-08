#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Sleep support for svc_power (docs/02-firmware-architecture.md §7). On the watch
// the CPU light-sleeps on its own whenever every task is idle (esp_pm); this only
// chooses which inputs may end it.

/** Wake sources for hal_power_arm_wake(). */
enum {
    HAL_WAKE_TOUCH = 1u << 0,   // touch interrupt (touch in low-power mode)
    HAL_WAKE_BUTTONS = 1u << 1, // BACK and POWER
    HAL_WAKE_RTC = 1u << 2,     // RTC alarm interrupt
    HAL_WAKE_MOTION = 1u << 3,  // IMU interrupt (wake-on-motion, svc_sensors)
};

/** Once at start: pin states kept through light sleep, GPIO wake-up enabled. */
esp_err_t hal_power_init(void);

/**
 * Let these sources end light sleep (screen off). A source that fires stays
 * disarmed until the driver that handles it re-arms it after the event, so a held
 * button or finger cannot wake the CPU over and over.
 */
esp_err_t hal_power_arm_wake(uint32_t sources);
/** Screen on: no source is armed (the CPU does not light-sleep then). */
void hal_power_disarm_wake(void);

/** Deep sleep until POWER is pressed or, if timer_us > 0, that many microseconds
 *  passed (WATCH-ONLY; the RTC timer runs on the internal RC clock, a few % off).
 *  keep_display: the panel stays on and keeps showing its last frame (its reset and
 *  select lines are held; the next boot attaches to it without a reset). Waking is a
 *  reset. Does not return on success. */
esp_err_t hal_power_deep_sleep(uint64_t timer_us, bool keep_display);

#ifdef __cplusplus
}
#endif
