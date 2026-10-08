// Input service (docs/02-firmware-architecture.md §7, docs/03 F3): hardware buttons
// -> short/long/double/triple presses -> actions, and palm-cover detection.
//
// No task of its own. Button edges arrive in the FreeRTOS timer task (bsp debounce)
// and the gesture deadlines run on one-shot FreeRTOS timers in that same task, so
// button handling is serialized; touch samples arrive in the touch task. Results are
// SVC_INPUT_EVENT events. A press while the screen is off only wakes it.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "button_gesture.h"
#include "esp_err.h"
#include "svc_input_events.h"

#ifdef __cplusplus
extern "C" {
#endif

/** PWR held this long: LONG (power menu, docs/03 F3). */
#define SVC_INPUT_POWER_LONG_MS 2000
/** BOOT held this long: LONG (shortcut). */
#define SVC_INPUT_BACK_LONG_MS 1000
/** Next click within this after a release continues a double/triple press. */
#define SVC_INPUT_MULTI_GAP_MS 300
/** Palm: covering samples for this long turn the screen off. */
#define SVC_INPUT_PALM_HOLD_MS 300

/**
 * After svc_power_start(): takes the HAL button and touch-contact callbacks.
 * Default map: BOOT short = BACK, BOOT long = SHORTCUT, PWR short = HOME,
 * PWR long = POWER_MENU, PWR triple = SOS.
 */
esp_err_t svc_input_start(void);

/**
 * Map a press to an action (SVC_INPUT_ACTION_NONE unmaps it). Any task. A button
 * waits for double/triple presses only if one is mapped, so an unmapped double
 * keeps its short press instant.
 */
esp_err_t svc_input_set_action(svc_input_button_t button, btn_gesture_t press, svc_input_action_t action);
svc_input_action_t svc_input_get_action(svc_input_button_t button, btn_gesture_t press);

typedef struct {
    uint32_t presses[SVC_INPUT_BUTTON_COUNT][BTN_GESTURE_COUNT]; // recognized presses since boot
    uint32_t wake_presses;                                       // presses that only woke the screen
    uint32_t palms;
    // Largest values seen during the last completed touch contact (palm tuning).
    uint8_t last_points;
    uint8_t last_area;
    uint16_t last_w; // bounding box of the tracked points
    uint16_t last_h;
    bool last_covered; // a sample covered by palm rules
} svc_input_stats_t;

void svc_input_get_stats(svc_input_stats_t *out);

const char *svc_input_button_name(svc_input_button_t button);
const char *svc_input_action_name(svc_input_action_t action);

#ifdef __cplusplus
}
#endif
