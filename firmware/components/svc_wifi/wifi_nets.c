// Saved Wi-Fi networks (wifi_nets.h). Pure C.
#include "wifi_nets.h"

#include <string.h>

static size_t bounded_len(const char *s, size_t max)
{
    size_t n = 0;
    while (n <= max && s[n]) {
        n++;
    }
    return n; // max + 1: too long
}

wifi_nets_err_t wifi_nets_check(const char *ssid, const char *pass)
{
    if (!ssid || !pass) {
        return WIFI_NETS_INVALID;
    }
    const size_t s = bounded_len(ssid, WIFI_SSID_MAX);
    const size_t p = bounded_len(pass, WIFI_PASS_MAX);
    if (s == 0 || s > WIFI_SSID_MAX || p > WIFI_PASS_MAX || (p > 0 && p < WIFI_PASS_MIN)) {
        return WIFI_NETS_INVALID;
    }
    return WIFI_NETS_OK;
}

int wifi_nets_find(const wifi_nets_t *n, const char *ssid)
{
    for (int i = 0; i < n->count; i++) {
        if (strcmp(n->net[i].ssid, ssid) == 0) {
            return i;
        }
    }
    return -1;
}

wifi_nets_err_t wifi_nets_add(wifi_nets_t *n, const char *ssid, const char *pass)
{
    const wifi_nets_err_t e = wifi_nets_check(ssid, pass);
    if (e != WIFI_NETS_OK) {
        return e;
    }
    int at = wifi_nets_find(n, ssid);
    if (at < 0) {
        if (n->count >= WIFI_NETS_MAX) {
            return WIFI_NETS_FULL;
        }
        at = n->count++;
    }
    // Shift the ones before it down a place; it becomes [0].
    memmove(&n->net[1], &n->net[0], (size_t)at * sizeof n->net[0]);
    memset(&n->net[0], 0, sizeof n->net[0]);
    strcpy(n->net[0].ssid, ssid);
    strcpy(n->net[0].pass, pass);
    return WIFI_NETS_OK;
}

wifi_nets_err_t wifi_nets_forget(wifi_nets_t *n, const char *ssid)
{
    const int at = ssid ? wifi_nets_find(n, ssid) : -1;
    if (at < 0) {
        return WIFI_NETS_NOT_FOUND;
    }
    memmove(&n->net[at], &n->net[at + 1], (size_t)(n->count - at - 1) * sizeof n->net[0]);
    n->count--;
    memset(&n->net[n->count], 0, sizeof n->net[0]);
    return WIFI_NETS_OK;
}

bool wifi_nets_valid(const wifi_nets_t *n)
{
    if (n->count > WIFI_NETS_MAX) {
        return false;
    }
    for (int i = 0; i < n->count; i++) {
        const wifi_net_t *w = &n->net[i];
        if (!memchr(w->ssid, '\0', sizeof w->ssid) || !memchr(w->pass, '\0', sizeof w->pass) ||
            wifi_nets_check(w->ssid, w->pass) != WIFI_NETS_OK) {
            return false;
        }
    }
    return true;
}

int wifi_nets_pick(const wifi_nets_t *n, const wifi_seen_t *seen, size_t count, int prefer, uint32_t skip_mask)
{
    int best = -1;
    int best_rssi = 0;
    for (size_t k = 0; k < count; k++) {
        const int i = wifi_nets_find(n, seen[k].ssid);
        if (i < 0 || (skip_mask & (1u << i))) {
            continue;
        }
        if (i == prefer) {
            return i;
        }
        // A scan lists one SSID once per access point: keep the strongest, then the most recent.
        if (best < 0 || seen[k].rssi > best_rssi || (seen[k].rssi == best_rssi && i < best)) {
            best = i;
            best_rssi = seen[k].rssi;
        }
    }
    return best;
}

uint32_t wifi_nets_backoff_ms(uint32_t fails)
{
    static const uint32_t k_ms[] = {5000, 15000, 30000, 60000};
    if (fails == 0) {
        return k_ms[0];
    }
    return fails <= sizeof k_ms / sizeof k_ms[0] ? k_ms[fails - 1] : 300000;
}
