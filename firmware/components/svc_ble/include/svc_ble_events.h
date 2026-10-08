#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(SVC_BLE_EVENT);

typedef enum {
    SVC_BLE_EVT_STATE,        // svc_ble_evt_state_t: link state or bond changed
    SVC_BLE_EVT_PAIR_REQUEST, // svc_ble_evt_pair_request_t: show the code, answer with svc_ble_pair_reply()
    SVC_BLE_EVT_PAIR_DONE,    // svc_ble_evt_pair_done_t: pairing finished (ok or not); close the code
    SVC_BLE_EVT_DISCONNECTED, // svc_ble_evt_disconnected_t: a link went down (metrics)
} svc_ble_event_t;

typedef enum {
    SVC_BLE_STATE_OFF,         // Bluetooth turned off, or the host is not running (not started, or reset)
    SVC_BLE_STATE_IDLE,        // running, not advertising, no link (advertising failed)
    SVC_BLE_STATE_ADVERTISING, // waiting for the phone
    SVC_BLE_STATE_CONNECTED,   // a link, but not the bonded companion over an encrypted link (yet)
    SVC_BLE_STATE_SECURED,     // the companion is connected over an authenticated, bonded link
} svc_ble_state_t;

typedef struct {
    uint8_t state;   // svc_ble_state_t
    bool paired;     // a companion bond exists
    uint8_t links;   // open connections
} svc_ble_evt_state_t;

typedef struct {
    uint16_t conn;    // pass back to svc_ble_pair_reply()
    uint32_t passkey; // 6-digit numeric comparison code (000000..999999)
    bool replaces;    // another phone is paired: confirming replaces it
} svc_ble_evt_pair_request_t;

typedef struct {
    uint16_t conn;
    bool ok;
    int status; // NimBLE status of the failure (0 when ok)
} svc_ble_evt_pair_done_t;

typedef struct {
    uint16_t reason;   // HCI reason (NimBLE BLE_HS_ERR_HCI_BASE + code)
    uint32_t seconds;  // how long the link was up
    bool companion;    // it was the secured companion link
} svc_ble_evt_disconnected_t;

#ifdef __cplusplus
}
#endif
