#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Touch ---------------------------------------------------------------------

/** Latest touch point in display pixels; true while pressed. Never blocks or does I/O. */
bool hal_touch_read(uint16_t *x, uint16_t *y);

/** Called on every touch-down (also the touch that ends low-power mode), from a
 *  driver task: short and non-blocking. One callback (svc_power). */
typedef void (*hal_touch_down_cb_t)(void *ctx);
void hal_touch_set_down_cb(hal_touch_down_cb_t cb, void *ctx);

/** One touch sample while in contact (~every 10 ms), and a final one with points = 0. */
typedef struct {
    uint8_t points;   // contacts reported by the controller; more than it tracks = large contact
    uint8_t area_max; // largest per-point contact area, controller units (0 = unknown)
    uint16_t x_min;   // bounding box of the tracked points, display pixels
    uint16_t y_min;
    uint16_t x_max;
    uint16_t y_max;
} hal_touch_contact_t;

/** From a driver task: short and non-blocking. One callback (svc_input palm detection). */
typedef void (*hal_touch_contact_cb_t)(const hal_touch_contact_t *c, void *ctx);
void hal_touch_set_contact_cb(hal_touch_contact_cb_t cb, void *ctx);

/** Low power while the screen is off: the controller scans slowly and a touch still
 *  raises its interrupt (tap to wake). Blocking I2C: not from the UI task. */
esp_err_t hal_touch_set_low_power(bool low_power);

// --- Buttons ---------------------------------------------------------------------

typedef enum {
    HAL_BUTTON_BACK = 0, // BOOT key on the watch, Esc/Backspace in the simulator
    HAL_BUTTON_POWER,    // PWR key (AXP2101), P in the simulator
    HAL_BUTTON_COUNT,
} hal_button_t;

/** Debounced level change. Short and non-blocking: runs in a timer/driver context. */
typedef void (*hal_button_cb_t)(hal_button_t button, bool pressed, void *ctx);

esp_err_t hal_buttons_set_callback(hal_button_cb_t cb, void *ctx);
bool hal_button_is_pressed(hal_button_t button);
const char *hal_button_name(hal_button_t button);

#ifdef __cplusplus
}
#endif
