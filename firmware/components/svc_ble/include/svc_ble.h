// BLE service (docs/06-ble-protocol.md §1, §8, docs/02-firmware-architecture.md §7, P4-01):
// NimBLE peripheral with the S3W Link service (RX / TX / BULK), Battery Service and
// Device Information; advertising as "S3Wear-XXXX"; LE Secure Connections with
// numeric comparison; bonds in NVS; one companion phone.
//
// Pairing: the phone starts it; svc_ble posts SVC_BLE_EVT_PAIR_REQUEST with the 6-digit
// code, the UI shows it and answers with svc_ble_pair_reply(). SVC_BLE_EVT_PAIR_DONE
// follows (accepted, rejected, timed out or link lost). A new bond deletes every
// other bond, so there is at most one companion; the old phone needs to pair again.
//
// The S3W characteristics need an authenticated (MITM), bonded, encrypted link; the
// GATT layer refuses everything else. Frames are opaque here: svc_link (P4-02)
// installs the RX handler and sends with svc_ble_send().
//
// API calls are safe from any task (NimBLE's host lock), including the UI task: none
// of them waits for the radio.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "svc_ble_events.h"

#ifdef __cplusplus
extern "C" {
#endif

/** S3W Link service and characteristic UUIDs (docs/06 §1), as strings for logs. */
#define SVC_BLE_LINK_UUID "7a3e0001-5a1b-4c8e-9f2d-3b6c1e0d4a77"

/** Model id in the advertising manufacturer data (docs/06 §1). */
#define SVC_BLE_MODEL_ID 0x01
/** Manufacturer data flag bit: a companion is paired. */
#define SVC_BLE_ADV_F_PAIRED 0x01

/**
 * After settings, svc_power (battery level) and NVS. Starts the NimBLE host task and
 * returns; advertising begins when the host syncs with the controller (~100 ms), unless
 * the BLUETOOTH setting is off.
 */
esp_err_t svc_ble_start(void);

/** Answer a SVC_BLE_EVT_PAIR_REQUEST: the codes match (accept) or not. */
esp_err_t svc_ble_pair_reply(uint16_t conn, bool accept);

/** Delete every bond and drop every link ("Forget phone", factory reset). */
esp_err_t svc_ble_forget(void);

/**
 * Bluetooth on / off (the BLUETOOTH setting; svc_ble follows it, so callers normally change
 * the setting). Off drops every link and stops advertising, keeping the bonds; the state is
 * SVC_BLE_STATE_OFF. On advertises fast again.
 */
esp_err_t svc_ble_set_enabled(bool on);

/**
 * The companion is not connected: advertise fast again (30 s) so the phone finds the watch
 * sooner. The phone is the central and makes the connection; nothing happens while the
 * companion is connected or Bluetooth is off.
 */
esp_err_t svc_ble_reconnect(void);

typedef enum {
    SVC_BLE_CHAN_CONTROL, // RX (write) / TX (notify)
    SVC_BLE_CHAN_BULK,    // BULK (write without response / notify)
} svc_ble_chan_t;

/** Frames written by the companion. Runs on the NimBLE host task: copy and return. */
typedef void (*svc_ble_rx_cb_t)(svc_ble_chan_t chan, const uint8_t *data, size_t len, void *ctx);

/** Install the frame handler (svc_link). NULL drops incoming frames. */
void svc_ble_set_rx(svc_ble_rx_cb_t cb, void *ctx);

/**
 * Notify one frame to the companion on TX or BULK. len ≤ svc_ble_mtu() − 3.
 * ESP_ERR_INVALID_STATE: no secured companion link or notifications not enabled;
 * ESP_ERR_NO_MEM: out of NimBLE buffers (retry later).
 */
esp_err_t svc_ble_send(svc_ble_chan_t chan, const uint8_t *data, size_t len);

/** ATT MTU of the companion link (23 when none). */
uint16_t svc_ble_mtu(void);

typedef struct {
    svc_ble_state_t state;
    bool enabled;              // Bluetooth on (setting BLUETOOTH)
    bool paired;
    uint8_t links;
    char name[16];             // "S3Wear-XXXX"
    uint8_t addr[6];           // own identity address, LSB first
    uint8_t addr_type;         // BLE_ADDR_PUBLIC / RANDOM
    uint8_t peer[6];           // companion identity address (paired), LSB first
    uint8_t peer_type;
    uint16_t conn;             // companion link handle (0xFFFF = none)
    uint16_t pair_conn;        // link with a pairing request waiting for an answer (0xFFFF = none)
    uint16_t mtu;
    bool tx_subscribed;
    bool bulk_subscribed;
    bool adv_fast;             // advertising at the fast interval
    uint32_t connects;         // since boot
    uint32_t pairings;         // successful pairings since boot
    uint32_t rx_frames;
    uint32_t tx_frames;
} svc_ble_status_t;

void svc_ble_get_status(svc_ble_status_t *out);

const char *svc_ble_state_name(svc_ble_state_t state);

#ifdef __cplusplus
}
#endif
