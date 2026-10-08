// Connections page of the Settings app (docs/03 F18, docs/04 §4e, P4-09): id "connections", and
// its Wi-Fi page (P9-01): id "wifi". Portable (LVGL + ui_framework): the simulator builds it too. UI task only, like ui_nav.h.
//
// The screen calls no service: the state and the actions go through a backend (app_main:
// svc_settings, svc_ble, svc_link, svc_wifi; simulator: sim_connect.c). Call
// connect_apps_changed() when the state may have changed (link, Bluetooth, sync, Wi-Fi): the open
// page re-reads it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONNECT_SSID_MAX       32 // bytes
#define CONNECT_WIFI_SAVED_MAX 5

typedef enum {
    CONNECT_WIFI_OFF,
    CONNECT_WIFI_IDLE,    // on, not joined
    CONNECT_WIFI_JOINING, // ssid
    CONNECT_WIFI_JOINED,  // ssid, rssi
} connect_wifi_state_t;

typedef enum {
    CONNECT_WIFI_ERR_NONE,
    CONNECT_WIFI_ERR_NOT_FOUND, // no saved network in range
    CONNECT_WIFI_ERR_AUTH,      // wrong password (error_ssid)
    CONNECT_WIFI_ERR_NO_IP,     // joined error_ssid, no address
    CONNECT_WIFI_ERR_OTHER,
} connect_wifi_err_t;

typedef struct {
    bool on;       // the WIFI setting
    uint8_t state; // connect_wifi_state_t
    uint8_t error; // connect_wifi_err_t, of the last attempt
    int8_t rssi;   // dBm, JOINED
    char ssid[CONNECT_SSID_MAX + 1];
    char error_ssid[CONNECT_SSID_MAX + 1];
    uint8_t saved_count;
    char saved[CONNECT_WIFI_SAVED_MAX][CONNECT_SSID_MAX + 1]; // most recent first
} connect_wifi_t;

typedef struct {
    bool bluetooth;     // the BLUETOOTH setting
    bool paired;        // a companion phone is paired
    bool connected;     // the companion session is up
    int32_t sync_age_s; // seconds since the last sync with the phone, -1 = none since boot
    connect_wifi_t wifi;
} connect_state_t;

typedef struct {
    void (*read)(connect_state_t *out, void *ctx);
    esp_err_t (*set_bluetooth)(bool on, void *ctx);
    esp_err_t (*reconnect)(void *ctx); // paired but not connected: make the watch easy to find
    esp_err_t (*forget)(void *ctx);    // delete the bond (confirmed by the user)
    esp_err_t (*set_wifi)(bool on, void *ctx);
    esp_err_t (*wifi_forget)(const char *ssid, void *ctx); // a saved network (confirmed by the user)
    void *ctx;
} connect_backend_t;

/** Register the screen (shell_init() does this). */
void connect_apps_init(void);

/** Install the backend (copied). Without one: Bluetooth and Wi-Fi off, nothing paired, changes "Not available". */
void connect_apps_set_backend(const connect_backend_t *backend);

/** The state may have changed: refresh the Connections or Wi-Fi page if it is visible. */
void connect_apps_changed(void);

#ifdef __cplusplus
}
#endif
