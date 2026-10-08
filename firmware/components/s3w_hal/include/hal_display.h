#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_DISPLAY_HRES 410
#define HAL_DISPLAY_VRES 502

/** Panel brightness 0..255 (0 = dark but still scanning out). */
esp_err_t hal_display_set_brightness(uint8_t level);
uint8_t hal_display_get_brightness(void);

/**
 * Panel power (svc_power). Off: display off + sleep in. On: sleep out + display
 * on, which blocks ~120 ms (panel wake time), so never call it from the UI task.
 * The panel shows whatever it holds at the current brightness: set brightness 0
 * first and raise it after the first new frame to hide a stale image.
 */
esp_err_t hal_display_set_power(bool on);

#ifdef __cplusplus
}
#endif
