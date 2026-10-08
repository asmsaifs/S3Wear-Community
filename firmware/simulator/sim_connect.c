/* Settings > Connections backend in the simulator (sim_connect.h). Script commands:
 *
 *   link connected|reconnecting|unpaired   the phone link state (Bluetooth stays as it is)
 *   link-sync <seconds>|never              age of the last sync with the phone
 *   link-bt-is on|off                      fail unless the Bluetooth switch is that
 *   link-asked reconnect|forget|none       fail unless the page's last request was that
 *   wifi off|searching|joining|joined|notfound|auth|noip   Wi-Fi state (the network: the first saved one)
 *   wifi-saved <ssid>[,<ssid>...]|none     the saved networks (default "Home")
 *   wifi-is on|off                         fail unless the Wi-Fi switch is that
 *   wifi-forgot <ssid>|none                fail unless the last network forgotten on the page was that */
#include "sim_connect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "connect_apps.h"

static connect_state_t s_st;
static const char *s_asked = "none";
static char s_forgot[CONNECT_SSID_MAX + 1] = "none";

static void be_read(connect_state_t *out, void *ctx)
{
    (void)ctx;
    *out = s_st;
    out->connected = s_st.bluetooth && s_st.connected;
}

static esp_err_t be_set_bluetooth(bool on, void *ctx)
{
    (void)ctx;
    printf("bluetooth: %s\n", on ? "on" : "off");
    s_st.bluetooth = on;
    s_st.connected = false; /* off drops the link; on waits for the phone */
    return ESP_OK;
}

static esp_err_t be_set_wifi(bool on, void *ctx)
{
    (void)ctx;
    printf("wifi: %s\n", on ? "on" : "off");
    s_st.wifi.on = on;
    s_st.wifi.state = on ? CONNECT_WIFI_IDLE : CONNECT_WIFI_OFF; /* on: searching */
    s_st.wifi.error = CONNECT_WIFI_ERR_NONE;
    return ESP_OK;
}

static esp_err_t be_wifi_forget(const char *ssid, void *ctx)
{
    (void)ctx;
    connect_wifi_t *w = &s_st.wifi;
    for (int i = 0; i < w->saved_count; i++) {
        if (strcmp(w->saved[i], ssid) == 0) {
            memmove(w->saved[i], w->saved[i + 1], (size_t)(w->saved_count - i - 1) * sizeof w->saved[0]);
            w->saved_count--;
            snprintf(s_forgot, sizeof s_forgot, "%s", ssid);
            if (w->state != CONNECT_WIFI_OFF && strcmp(w->ssid, ssid) == 0) {
                w->state = CONNECT_WIFI_IDLE; /* left it */
                w->ssid[0] = '\0';
            }
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t be_reconnect(void *ctx)
{
    (void)ctx;
    s_asked = "reconnect";
    return ESP_OK;
}

static esp_err_t be_forget(void *ctx)
{
    (void)ctx;
    s_asked = "forget";
    s_st.paired = false;
    s_st.connected = false;
    return ESP_OK;
}

bool sim_connect_bluetooth(void)
{
    return s_st.bluetooth;
}

void sim_connect_set_bluetooth(bool on)
{
    be_set_bluetooth(on, NULL);
    connect_apps_changed();
}

bool sim_connect_wifi(void)
{
    return s_st.wifi.on;
}

void sim_connect_set_wifi(bool on)
{
    be_set_wifi(on, NULL);
    connect_apps_changed();
}

void sim_connect_init(void)
{
    s_st = (connect_state_t){.bluetooth = true, .paired = true, .connected = true, .sync_age_s = 300};
    s_st.wifi.saved_count = 1;
    snprintf(s_st.wifi.saved[0], sizeof s_st.wifi.saved[0], "Home");
    const connect_backend_t be = {
        .read = be_read,
        .set_bluetooth = be_set_bluetooth,
        .reconnect = be_reconnect,
        .forget = be_forget,
        .set_wifi = be_set_wifi,
        .wifi_forget = be_wifi_forget,
    };
    connect_apps_set_backend(&be);
}

static bool wifi_state(const char *arg)
{
    static const struct {
        const char *name;
        uint8_t state;
        uint8_t error;
    } k[] = {
        {"off", CONNECT_WIFI_OFF, CONNECT_WIFI_ERR_NONE},
        {"searching", CONNECT_WIFI_IDLE, CONNECT_WIFI_ERR_NONE},
        {"joining", CONNECT_WIFI_JOINING, CONNECT_WIFI_ERR_NONE},
        {"joined", CONNECT_WIFI_JOINED, CONNECT_WIFI_ERR_NONE},
        {"notfound", CONNECT_WIFI_IDLE, CONNECT_WIFI_ERR_NOT_FOUND},
        {"auth", CONNECT_WIFI_IDLE, CONNECT_WIFI_ERR_AUTH},
        {"noip", CONNECT_WIFI_IDLE, CONNECT_WIFI_ERR_NO_IP},
    };
    connect_wifi_t *w = &s_st.wifi;
    const char *first = w->saved_count ? w->saved[0] : "";
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        if (strcmp(arg, k[i].name) == 0) {
            w->on = k[i].state != CONNECT_WIFI_OFF;
            w->state = k[i].state;
            w->error = k[i].error;
            w->rssi = -55;
            snprintf(w->ssid, sizeof w->ssid, "%s", k[i].state >= CONNECT_WIFI_JOINING ? first : "");
            snprintf(w->error_ssid, sizeof w->error_ssid, "%s",
                     k[i].error == CONNECT_WIFI_ERR_AUTH || k[i].error == CONNECT_WIFI_ERR_NO_IP ? first : "");
            connect_apps_changed();
            return true;
        }
    }
    return false;
}

static bool expect(const char *what, const char *got, const char *want)
{
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "script: %s is %s, not %s\n", what, got, want);
        return false;
    }
    return true;
}

bool sim_connect_cmd(const char *cmd, const char *arg)
{
    if (strcmp(cmd, "link") == 0) {
        if (strcmp(arg, "connected") == 0 || strcmp(arg, "reconnecting") == 0) {
            s_st.paired = true;
            s_st.connected = arg[0] == 'c';
        } else if (strcmp(arg, "unpaired") == 0) {
            s_st.paired = false;
            s_st.connected = false;
        } else {
            return false;
        }
        connect_apps_changed();
        return true;
    }
    if (strcmp(cmd, "link-sync") == 0) {
        char *end;
        const long v = strtol(arg, &end, 10);
        if (strcmp(arg, "never") == 0) {
            s_st.sync_age_s = -1;
        } else if (end != arg && *end == '\0' && v >= 0) {
            s_st.sync_age_s = (int32_t)v;
        } else {
            return false;
        }
        connect_apps_changed();
        return true;
    }
    if (strcmp(cmd, "link-bt-is") == 0) {
        return expect("Bluetooth", s_st.bluetooth ? "on" : "off", arg);
    }
    if (strcmp(cmd, "link-asked") == 0) {
        return expect("the last request", s_asked, arg);
    }
    if (strcmp(cmd, "wifi") == 0) {
        return wifi_state(arg);
    }
    if (strcmp(cmd, "wifi-saved") == 0) {
        connect_wifi_t *w = &s_st.wifi;
        w->saved_count = 0;
        memset(w->saved, 0, sizeof w->saved);
        const char *p = strcmp(arg, "none") == 0 ? "" : arg;
        while (*p && w->saved_count < CONNECT_WIFI_SAVED_MAX) {
            const size_t n = strcspn(p, ",");
            snprintf(w->saved[w->saved_count++], sizeof w->saved[0], "%.*s", (int)n, p);
            p += n + (p[n] == ',');
        }
        connect_apps_changed();
        return true;
    }
    if (strcmp(cmd, "wifi-is") == 0) {
        return expect("Wi-Fi", s_st.wifi.on ? "on" : "off", arg);
    }
    if (strcmp(cmd, "wifi-forgot") == 0) {
        return expect("the network forgotten", s_forgot, arg);
    }
    return false;
}
