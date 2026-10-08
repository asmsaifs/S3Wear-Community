// Saved Wi-Fi networks and the choice of which to join (svc_wifi, P9-01). Pure C, no IDF: also
// built by firmware/host_test.
//
// Up to WIFI_NETS_MAX networks, most recently added first. The list is stored as one NVS blob
// (the struct itself): wifi_nets_valid() checks a blob read back.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_NETS_MAX 5
#define WIFI_SSID_MAX 32 // bytes, 802.11
#define WIFI_PASS_MIN 8  // WPA passphrase
#define WIFI_PASS_MAX 63

typedef struct {
    char ssid[WIFI_SSID_MAX + 1];
    char pass[WIFI_PASS_MAX + 1]; // empty: open network
} wifi_net_t;

typedef struct {
    uint8_t count;
    wifi_net_t net[WIFI_NETS_MAX]; // [0] most recently added
} wifi_nets_t;

typedef enum {
    WIFI_NETS_OK,
    WIFI_NETS_INVALID,   // ssid empty or too long, password not empty and not 8-63 bytes
    WIFI_NETS_FULL,      // a new ssid with WIFI_NETS_MAX saved
    WIFI_NETS_NOT_FOUND, // forget: not saved
} wifi_nets_err_t;

/** Lengths only (an SSID may hold any bytes but NUL). */
wifi_nets_err_t wifi_nets_check(const char *ssid, const char *pass);

/** Save ssid (replacing its password if saved) as the most recent. */
wifi_nets_err_t wifi_nets_add(wifi_nets_t *n, const char *ssid, const char *pass);

wifi_nets_err_t wifi_nets_forget(wifi_nets_t *n, const char *ssid);

/** Index of ssid, -1 if not saved. */
int wifi_nets_find(const wifi_nets_t *n, const char *ssid);

/** A blob read back from flash is a list (count in range, strings terminated, lengths valid). */
bool wifi_nets_valid(const wifi_nets_t *n);

/** One access point from a scan. */
typedef struct {
    char ssid[WIFI_SSID_MAX + 1];
    int8_t rssi; // dBm
} wifi_seen_t;

/**
 * The saved network to join from a scan: `prefer` (an index, -1 = none) if it was seen, else the
 * strongest saved network seen, the most recent one on a tie. Saved networks whose bit is set in
 * skip_mask (bit i = net[i], e.g. the password just failed) are left out. -1: none.
 */
int wifi_nets_pick(const wifi_nets_t *n, const wifi_seen_t *seen, size_t count, int prefer, uint32_t skip_mask);

/** Wait before the next scan after `fails` attempts in a row (1, 2, ...): 5 s, 15 s, 30 s, 60 s, then 5 min. */
uint32_t wifi_nets_backoff_ms(uint32_t fails);

#ifdef __cplusplus
}
#endif
