// Wi-Fi provisioning over the link (docs/06 §4 "Wi-Fi"): WifiConfig requests from the phone
// (link task) and a WifiStatus event on every change while the link is up (bus task).
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "s3w_event.h"
#include "svc_link.h"
#include "svc_wifi.h"
#include "wifi_priv.h"

static const char *TAG = "wifi_link";

#define PSRAM (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

static void fill(s3w_v1_Envelope *env)
{
    svc_wifi_status_t st;
    svc_wifi_get(&st);
    env->which_body = s3w_v1_Envelope_wifi_status_tag;
    s3w_v1_WifiStatus *m = &env->body.wifi_status;
    m->on = st.on;
    m->state = st.state;
    m->error = st.error;
    snprintf(m->error_ssid, sizeof m->error_ssid, "%s", st.error_ssid);
    if (st.state == SVC_WIFI_CONNECTING || st.state == SVC_WIFI_CONNECTED) {
        snprintf(m->ssid, sizeof m->ssid, "%s", st.ssid);
    }
    if (st.state == SVC_WIFI_CONNECTED) {
        const uint8_t *b = (const uint8_t *)&st.ip; // network byte order
        snprintf(m->ip, sizeof m->ip, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
        m->rssi = st.rssi;
    }
    m->saved_count = st.saved_count;
    for (int i = 0; i < st.saved_count; i++) {
        snprintf(m->saved[i], sizeof m->saved[i], "%s", st.saved[i]);
    }
}

// Link task.
static void on_config(void *ctx, const s3w_v1_Envelope *req)
{
    (void)ctx;
    if (req->id == 0) {
        return; // a change must be a request: the phone needs its answer
    }
    const s3w_v1_WifiConfig *c = &req->body.wifi_config;
    esp_err_t err = ESP_OK;
    switch (c->op) {
    case s3w_v1_WifiOp_WIFI_OP_GET:
        break;
    case s3w_v1_WifiOp_WIFI_OP_ADD:
        err = svc_wifi_add(c->ssid, c->password);
        break;
    case s3w_v1_WifiOp_WIFI_OP_FORGET:
        err = svc_wifi_forget(c->ssid);
        break;
    case s3w_v1_WifiOp_WIFI_OP_SET_ON:
        err = svc_wifi_set_on(c->on);
        break;
    default:
        svc_link_reply_status(req, s3w_v1_StatusCode_STATUS_UNSUPPORTED, "unknown op");
        return;
    }
    const int32_t code = err == ESP_OK                ? s3w_v1_StatusCode_STATUS_OK
                         : err == ESP_ERR_NO_MEM      ? s3w_v1_StatusCode_STATUS_NO_SPACE
                         : err == ESP_ERR_INVALID_ARG ? s3w_v1_StatusCode_STATUS_INVALID
                         : err == ESP_ERR_NOT_FOUND   ? s3w_v1_StatusCode_STATUS_INVALID
                                                      : s3w_v1_StatusCode_STATUS_INTERNAL;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WifiConfig op %lu: %s", (unsigned long)c->op, esp_err_to_name(err));
    }
    s3w_v1_Envelope *r = heap_caps_calloc(1, sizeof *r, PSRAM);
    if (!r) {
        svc_link_reply_status(req, s3w_v1_StatusCode_STATUS_INTERNAL, "no memory");
        return;
    }
    fill(r);
    r->has_status = true;
    r->status.code = code;
    svc_link_reply(req, r);
    heap_caps_free(r);
}

// Bus task.
static void on_state(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    (void)id;
    (void)data;
    (void)len;
    if (!svc_link_is_up()) {
        return; // the phone asks (WIFI_OP_GET) at the next connect
    }
    s3w_v1_Envelope *env = heap_caps_calloc(1, sizeof *env, PSRAM);
    if (!env) {
        return;
    }
    fill(env);
    if (svc_link_send(env) != ESP_OK) {
        ESP_LOGD(TAG, "WifiStatus not sent");
    }
    heap_caps_free(env);
}

esp_err_t wifi_link_start(void)
{
    ESP_RETURN_ON_ERROR(svc_link_register(s3w_v1_Envelope_wifi_config_tag, on_config, NULL), TAG, "link");
    return s3w_event_subscribe(SVC_WIFI_EVENT, SVC_WIFI_EVT_STATE, on_state, NULL, NULL);
}
