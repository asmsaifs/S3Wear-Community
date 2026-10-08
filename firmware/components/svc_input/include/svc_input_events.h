#pragma once

#include <stdint.h>
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(SVC_INPUT_EVENT);

typedef enum {
    SVC_INPUT_EVT_BUTTON, // svc_input_evt_button_t, every recognized press while the screen is on
    SVC_INPUT_EVT_ACTION, // svc_input_evt_action_t, the press is mapped to an action
    SVC_INPUT_EVT_PALM,   // no payload: palm cover detected, the screen is being turned off
} svc_input_event_t;

typedef enum {
    SVC_INPUT_BUTTON_BACK = 0, // BOOT key
    SVC_INPUT_BUTTON_POWER,    // PWR key
    SVC_INPUT_BUTTON_COUNT,
} svc_input_button_t;

typedef struct {
    uint8_t button; // svc_input_button_t
    uint8_t press;  // btn_gesture_t (button_gesture.h)
} svc_input_evt_button_t;

typedef enum {
    SVC_INPUT_ACTION_NONE = 0,
    SVC_INPUT_ACTION_BACK,       // ui_nav_back()
    SVC_INPUT_ACTION_HOME,       // ui_nav_home(), or screen off on the home screen
    SVC_INPUT_ACTION_POWER_MENU, // power menu (docs/03 F3)
    SVC_INPUT_ACTION_SHORTCUT,   // user shortcut (default flashlight, P3)
    SVC_INPUT_ACTION_SOS,        // SOS (docs/03 F3; phone side in P4)
    SVC_INPUT_ACTION_COUNT,
} svc_input_action_t;

typedef struct {
    uint8_t action; // svc_input_action_t
    uint8_t button; // svc_input_button_t
    uint8_t press;  // btn_gesture_t
} svc_input_evt_action_t;

#ifdef __cplusplus
}
#endif
