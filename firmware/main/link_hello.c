// Handshake with the companion (docs/06 §4): Hello → HelloAck. Runs on the svc_link task.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "link_frame.h"
#include "link_hello.h"
#include "s3w_edition.h"
#include "svc_link.h"
#include "svc_power.h"
#include "svc_storage.h"

static const char *TAG = "link_hello";

#define PROTO_MINOR 0
#define HW_REV      "206"
#define API_LEVEL   2

// Capabilities (docs/06 §4), added with the features they name. The Community edition has
// none of them (docs/10 §3). NULL ends the list.
static const char *const CAPS[] = {
#if S3W_EDITION_PRO
    "notif.actions",     // NotificationAction requests and dismissals (P4-07)
    "notif.bitmap_text", // TextBitmap for scripts the fonts lack (P4-07)
    "media",             // MediaState / MediaCommand / MediaArtwork (P6-01)
    "media.bitmap_text", // media_text_bitmap: titles in scripts the fonts lack (P6-01)
    "weather",           // WeatherUpdate (P6-02)
    "calendar",          // CalendarUpdate (P6-03)
    "call",              // CallState / CallCommand (P6-04)
    "apps.v1",           // AppList / AppInstallBegin / TRANSFER_APP / AppCommand (P8-09)
    "wifi.prov",         // WifiConfig / WifiStatus (P9-01)
    "ha",                // HaConfig / HaCommand / HaStates (P9-05)
    "memo.v1",           // TRANSFER_MEMO_UP: voice memos to the phone (P7-01)
    "screenshot.v1",     // ScreenshotRequest, TRANSFER_SCREENSHOT_UP (P8-19)
    "license.v1",        // LicenseInstall / LicenseStatus (P12-03)
#endif
    NULL,
};

static void fill_ack(s3w_v1_HelloAck *a)
{
    a->proto_major = LINK_PROTO_MAJOR;
    a->proto_minor = PROTO_MINOR;
    snprintf(a->fw_version, sizeof a->fw_version, "%s", esp_app_get_description()->version);
    snprintf(a->hw_rev, sizeof a->hw_rev, "%s", HW_REV);
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(a->serial, sizeof a->serial, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    svc_power_battery_t b;
    if (svc_power_battery(&b) == ESP_OK && b.percent >= 0) {
        a->battery_pct = (uint32_t)b.percent;
    }
    uint64_t total = 0, used = 0;
    if (svc_storage_usage(SVC_STORAGE_FLASH_PATH, &total, &used) == ESP_OK) {
        a->storage_total_kb = (uint32_t)(total / 1024);
        a->storage_free_kb = (uint32_t)((total - used) / 1024);
    }
    a->api_level = API_LEVEL;
    for (size_t i = 0; CAPS[i]; i++) {
        snprintf(a->caps[a->caps_count++], sizeof a->caps[0], "%s", CAPS[i]);
    }
}

static void on_hello(void *ctx, const s3w_v1_Envelope *req)
{
    (void)ctx;
    const s3w_v1_Hello *h = &req->body.hello;
    ESP_LOGI(TAG, "hello: protocol %u.%u, app %s, phone %s", (unsigned)h->proto_major, (unsigned)h->proto_minor,
             h->app_version, h->phone_model);
    // Heap (PSRAM: over 4 KB), not the stack or .bss: an Envelope is over 8 KB.
    s3w_v1_Envelope *reply = calloc(1, sizeof *reply);
    if (!reply) {
        svc_link_reply_status(req, s3w_v1_StatusCode_STATUS_INTERNAL, "no memory");
        return;
    }
    reply->which_body = s3w_v1_Envelope_hello_ack_tag;
    fill_ack(&reply->body.hello_ack);
    reply->has_status = true;
    reply->status.code = s3w_v1_StatusCode_STATUS_OK;
    if (h->proto_major != LINK_PROTO_MAJOR) {
        // docs/06 §7: the phone tells the user to update.
        reply->status.code = s3w_v1_StatusCode_STATUS_UNSUPPORTED;
    }
    svc_link_reply(req, reply);
    free(reply);
}

esp_err_t link_hello_register(void)
{
    return svc_link_register(s3w_v1_Envelope_hello_tag, on_hello, NULL);
}
