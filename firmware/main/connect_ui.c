// Settings > Connections and Wi-Fi glue (P4-09, P9-01). Every backend call is quick and never
// waits for the radio (svc_ble queues onto its host task, svc_wifi onto the worker, svc_settings
// persists on the worker), so the pages call them from the UI task. The pages refresh on the
// events below (UI task): no timer.
#include "connect_ui.h"

#include <stdio.h>

#include "connect_apps.h"
#include "esp_event.h"
#include "link_time.h"
#include "s3w_edition.h"
#include "s3w_event.h"
#include "svc_ble.h"
#include "svc_link.h"
#include "svc_settings.h"
#include "svc_time.h"
#if S3W_EDITION_PRO
#include "svc_wifi.h"
#endif

static void be_read(connect_state_t *out, void *ctx)
{
    (void)ctx;
    svc_ble_status_t ble;
    svc_ble_get_status(&ble);
    out->bluetooth = svc_settings_get_bool(S3W_SETTING_BLUETOOTH);
    out->paired = ble.paired;
    out->connected = out->bluetooth && svc_link_is_up();
    out->sync_age_s = link_time_sync_age_s();
#if S3W_EDITION_PRO
    svc_wifi_status_t w;
    svc_wifi_get(&w);
    // connect_wifi_state_t and connect_wifi_err_t follow svc_wifi's order.
    out->wifi = (connect_wifi_t){.on = w.on, .state = (uint8_t)w.state, .error = (uint8_t)w.error, .rssi = w.rssi,
                                 .saved_count = w.saved_count};
    snprintf(out->wifi.ssid, sizeof out->wifi.ssid, "%s", w.ssid);
    snprintf(out->wifi.error_ssid, sizeof out->wifi.error_ssid, "%s", w.error_ssid);
    for (int i = 0; i < w.saved_count && i < CONNECT_WIFI_SAVED_MAX; i++) {
        snprintf(out->wifi.saved[i], sizeof out->wifi.saved[i], "%s", w.saved[i]);
    }
#endif
}

static esp_err_t be_set_bluetooth(bool on, void *ctx)
{
    (void)ctx;
    return svc_settings_set_bool(S3W_SETTING_BLUETOOTH, on); // svc_ble follows the setting
}

#if S3W_EDITION_PRO
static esp_err_t be_set_wifi(bool on, void *ctx)
{
    (void)ctx;
    return svc_wifi_set_on(on); // the WIFI setting; svc_wifi follows it
}

static esp_err_t be_wifi_forget(const char *ssid, void *ctx)
{
    (void)ctx;
    return svc_wifi_forget(ssid);
}
#endif

static esp_err_t be_reconnect(void *ctx)
{
    (void)ctx;
    return svc_ble_reconnect();
}

static esp_err_t be_forget(void *ctx)
{
    (void)ctx;
    return svc_ble_forget(); // the phone must pair again
}

// UI task: BLE / link state, a TimeSync (it sets the clock), Wi-Fi, the BLUETOOTH and WIFI settings.
static void on_change(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)data;
    (void)len;
    if (base == SVC_SETTINGS_EVENT && id == SVC_SETTINGS_EVT_CHANGED && len >= sizeof(svc_settings_evt_changed_t) &&
        ((const svc_settings_evt_changed_t *)data)->id != S3W_SETTING_BLUETOOTH &&
        ((const svc_settings_evt_changed_t *)data)->id != S3W_SETTING_WIFI) {
        return;
    }
    connect_apps_changed();
}

void connect_ui_start(void)
{
    const connect_backend_t be = {
        .read = be_read,
        .set_bluetooth = be_set_bluetooth,
        .reconnect = be_reconnect,
        .forget = be_forget,
#if S3W_EDITION_PRO
        .set_wifi = be_set_wifi,
        .wifi_forget = be_wifi_forget,
#endif
    };
    connect_apps_set_backend(&be);
    s3w_ui_subscribe(SVC_BLE_EVENT, SVC_BLE_EVT_STATE, on_change, NULL, NULL);
    s3w_ui_subscribe(SVC_LINK_EVENT, SVC_LINK_EVT_STATE, on_change, NULL, NULL);
    s3w_ui_subscribe(SVC_TIME_EVENT, SVC_TIME_EVT_CHANGED, on_change, NULL, NULL);
#if S3W_EDITION_PRO
    s3w_ui_subscribe(SVC_WIFI_EVENT, SVC_WIFI_EVT_STATE, on_change, NULL, NULL);
#endif
    s3w_ui_subscribe(SVC_SETTINGS_EVENT, ESP_EVENT_ANY_ID, on_change, NULL, NULL);
}
