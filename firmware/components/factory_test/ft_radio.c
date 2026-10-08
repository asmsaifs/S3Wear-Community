// Radio checks for the factory test. Both stacks are brought up only for the test
// and torn down again so normal boot keeps their RAM (svc_ble/svc_wifi own them later).
#include "ft_radio.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

static const char *TAG = "ft_radio";

#define BLE_SYNC_TIMEOUT_MS 3000

static SemaphoreHandle_t s_ble_sync;

static void ble_on_sync(void)
{
    xSemaphoreGive(s_ble_sync);
}

static void ble_host_task(void *arg)
{
    (void)arg;
    nimble_port_run(); // returns after nimble_port_stop()
    nimble_port_freertos_deinit();
}

static int ble_gap_cb(struct ble_gap_event *event, void *arg)
{
    (void)event;
    (void)arg;
    return 0;
}

ft_status_t ft_ble_advertise(uint32_t adv_ms, char *detail, size_t len)
{
    if (!s_ble_sync) {
        s_ble_sync = xSemaphoreCreateBinary();
    }
    xSemaphoreTake(s_ble_sync, 0);
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        snprintf(detail, len, "nimble init %s", esp_err_to_name(err));
        return FT_FAIL;
    }
    ble_hs_cfg.sync_cb = ble_on_sync;
    nimble_port_freertos_init(ble_host_task);

    ft_status_t st = FT_FAIL;
    uint8_t own_addr_type = 0;
    if (xSemaphoreTake(s_ble_sync, pdMS_TO_TICKS(BLE_SYNC_TIMEOUT_MS)) != pdTRUE) {
        snprintf(detail, len, "host sync timeout");
    } else if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        snprintf(detail, len, "no BLE address");
    } else {
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_BT);
        char name[16];
        snprintf(name, sizeof name, "S3W-FT-%02X%02X", mac[4], mac[5]);
        struct ble_hs_adv_fields fields = {0};
        fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
        fields.name = (const uint8_t *)name;
        fields.name_len = (uint8_t)strlen(name);
        fields.name_is_complete = 1;
        const struct ble_gap_adv_params params = {
            .conn_mode = BLE_GAP_CONN_MODE_NON,
            .disc_mode = BLE_GAP_DISC_MODE_GEN,
        };
        int rc = ble_gap_adv_set_fields(&fields);
        if (rc == 0) {
            rc = ble_gap_adv_start(own_addr_type, NULL, (int32_t)adv_ms, &params, ble_gap_cb, NULL);
        }
        if (rc == 0) {
            vTaskDelay(pdMS_TO_TICKS(adv_ms));
            ble_gap_adv_stop();
            snprintf(detail, len, "advertised as %s", name);
            st = FT_PASS;
        } else {
            snprintf(detail, len, "adv start rc=%d", rc);
        }
    }
    nimble_port_stop();
    nimble_port_deinit();
    return st;
}

ft_status_t ft_wifi_scan(char *detail, size_t len)
{
    static bool s_netif_ready;
    if (!s_netif_ready) {
        if (esp_netif_init() != ESP_OK || !esp_netif_create_default_wifi_sta()) {
            snprintf(detail, len, "netif init failed");
            return FT_FAIL;
        }
        s_netif_ready = true;
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        snprintf(detail, len, "wifi init %s", esp_err_to_name(err));
        return FT_FAIL;
    }
    ft_status_t st = FT_FAIL;
    uint16_t n = 0;
    wifi_ap_record_t best = {0};
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) {
        err = esp_wifi_set_mode(WIFI_MODE_STA);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err == ESP_OK) {
        err = esp_wifi_scan_start(NULL, true);
    }
    if (err == ESP_OK) {
        err = esp_wifi_scan_get_ap_num(&n);
    }
    if (err == ESP_OK && n > 0) {
        uint16_t one = 1;
        esp_wifi_scan_get_ap_records(&one, &best); // strongest first
    }
    esp_wifi_clear_ap_list();
    if (err != ESP_OK) {
        snprintf(detail, len, "scan %s", esp_err_to_name(err));
    } else if (n == 0) {
        snprintf(detail, len, "no access points found");
    } else {
        snprintf(detail, len, "%u APs, best %d dBm", n, best.rssi);
        st = FT_PASS;
    }
    esp_wifi_stop();
    esp_wifi_deinit();
    ESP_LOGI(TAG, "wifi: %s", detail);
    return st;
}
