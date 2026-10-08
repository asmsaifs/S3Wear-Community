// Wi-Fi (docs/03 F18, docs/02 §7 "Wi-Fi", docs/06 §4 "Wi-Fi", P9-01): station mode only.
//
// Follows the WIFI setting (off by default). On: scans, joins the strongest saved network in
// range (≤ WIFI_NETS_MAX, provisioned from the phone over the bonded link or the console), with
// DTIM modem sleep; on a failure scans again after 5 s, 15 s, 30 s, 60 s, then every 5 min.
// Joined: SNTP sets the clock (svc_time, source SNTP) at once and every 6 h. Off: the driver is
// deinitialised, which gives its internal RAM back.
//
// Idle: with nobody holding it (svc_wifi_acquire()), Wi-Fi switches itself off (setting WIFI) after
// WIFI_IDLE_OFF minutes (default 10, 0 = never), counted from the switch going on, a join, an added
// network or the last release.
//
// RAM: the driver needs internal RAM the watch only has with less elsewhere; a hook
// (svc_wifi_set_ram_hook) frees some before the driver starts and takes it back after it stops.
//
// All radio work runs on svc_worker; IDF events are copied there. Every function here is safe
// from any task except an ISR, including the UI: copies and list edits under a lock, nothing
// waits for the radio or flash (the list is written to NVS on the worker).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "svc_wifi_events.h"
#include "wifi_nets.h"

#ifdef __cplusplus
extern "C" {
#endif

// Same values as the protocol's WifiState and WifiError (envelope.proto).
typedef enum {
    SVC_WIFI_OFF = 0,
    SVC_WIFI_IDLE = 1,       // on, not joined (nothing in range or the last attempt failed)
    SVC_WIFI_CONNECTING = 2, // joining `ssid`
    SVC_WIFI_CONNECTED = 3,  // joined `ssid`, has an address
} svc_wifi_state_t;

typedef enum {
    SVC_WIFI_ERR_NONE = 0,
    SVC_WIFI_ERR_NOT_FOUND = 1, // no saved network in range
    SVC_WIFI_ERR_AUTH = 2,      // wrong password
    SVC_WIFI_ERR_NO_IP = 3,     // joined, no DHCP address
    SVC_WIFI_ERR_OTHER = 4,
} svc_wifi_err_t;

typedef struct {
    bool on; // the WIFI setting
    svc_wifi_state_t state;
    svc_wifi_err_t error;              // of the last attempt; NONE once joined
    char ssid[WIFI_SSID_MAX + 1];      // CONNECTING, CONNECTED
    char error_ssid[WIFI_SSID_MAX + 1]; // the network `error` is about ("" for NOT_FOUND)
    int8_t rssi;                       // CONNECTED: dBm when joined
    uint32_t ip;                       // CONNECTED: IPv4, network byte order (esp_ip4_addr_t.addr)
    uint8_t saved_count;
    char saved[WIFI_NETS_MAX][WIFI_SSID_MAX + 1]; // most recently added first
    uint8_t holders;                   // svc_wifi_acquire() without release
    uint32_t sntp_syncs;               // since boot
    int64_t sntp_last_ms;              // UTC ms of the last SNTP sync, 0 = none
} svc_wifi_status_t;

/**
 * Boot, after svc_worker_start() and the default event loop, before the display allocates its draw
 * buffers: lwIP and the station netif, one hardware AES operation, the SNTP server name, then the
 * driver initialised once (no radio) with one API call from each task that calls it or lwIP later
 * (worker, event loop, lwIP), then deinitialised. That creates what they keep for good after first
 * use (the driver's API lock, a Wi-Fi and an lwIP semaphore per calling task, the AES interrupt and
 * its lock); made later, while the RAM hook has freed a draw buffer, it would land in that space
 * and the buffer could not come back (each found with heap tracing on the watch).
 */
esp_err_t svc_wifi_prewarm(void);

/** After svc_settings_init(), svc_worker_start() and svc_link_start(): saved list from NVS, follow WIFI. */
esp_err_t svc_wifi_start(void);

/** The switch: sets the WIFI setting, which svc_wifi follows. */
esp_err_t svc_wifi_set_on(bool on);

/**
 * Save a network (replacing the password of a saved one), turn Wi-Fi on and join it if it is in
 * range. ESP_ERR_INVALID_ARG: ssid 1-32 bytes, password empty (open) or 8-63 bytes;
 * ESP_ERR_NO_MEM: WIFI_NETS_MAX saved already.
 */
esp_err_t svc_wifi_add(const char *ssid, const char *pass);

/** Remove a saved network (and leave it if joined). ESP_ERR_NOT_FOUND: not saved. */
esp_err_t svc_wifi_forget(const char *ssid);

/**
 * Something needs Wi-Fi (a download, OTA): it does not switch off for idleness until the matching
 * svc_wifi_release(). Does not switch it on.
 */
void svc_wifi_acquire(void);
void svc_wifi_release(void);

/**
 * Called on the worker with true before the driver starts (free internal RAM for it; the start is
 * refused if still too little) and false after it stopped or failed to start (take it back).
 */
typedef void (*svc_wifi_ram_hook_t)(bool radio_on);

/** Before svc_wifi_start(). */
void svc_wifi_set_ram_hook(svc_wifi_ram_hook_t hook);

/** Ask the time server again now (joined only; ESP_ERR_INVALID_STATE otherwise). */
esp_err_t svc_wifi_sntp_sync(void);

void svc_wifi_get(svc_wifi_status_t *out);

#ifdef __cplusplus
}
#endif
