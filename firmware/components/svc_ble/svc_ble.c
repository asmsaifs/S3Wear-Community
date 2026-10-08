// BLE service (svc_ble.h, docs/06-ble-protocol.md §1 and §8).
//
// Everything that changes the link state runs on the NimBLE host task: GAP events,
// GATT access, and the requests other tasks queue onto the host's event queue
// (forget, advertising refresh). The state the API reads is copied under a spinlock.
//
// Advertising (the only periodic radio wake-up while no phone is connected): 100 ms
// for 30 s after boot or a disconnect, so the phone finds the watch quickly, then
// 1 s until a phone connects (each event is ~1 ms of radio; docs/02 §7). It stops
// while the companion is connected over its secured link.
//
// Bluetooth off (setting BLUETOOTH, P4-09): the host keeps running but every link is
// dropped and advertising stops, so the radio stays silent; on brings advertising back
// (fast). Bonds are kept.
#include "svc_ble.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "s3w_event.h"
#include "services/bas/ble_svc_bas.h"
#include "services/dis/ble_svc_dis.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "svc_power.h"
#include "svc_settings.h"

static const char *TAG = "svc_ble";

ESP_EVENT_DEFINE_BASE(SVC_BLE_EVENT);

void ble_store_config_init(void); // NimBLE NVS bond store (no public header)

#define CONN_NONE         0xFFFF
#define MAX_LINKS         2     // companion + one more (a stranger or, later, a HID host)
#define ADV_FAST_MS       100
#define ADV_SLOW_MS       1000
#define ADV_FAST_TIME_MS  30000
#define ATT_MTU_DEFAULT   23
#define ATT_MTU_MAX       517
#define DLE_TX_OCTETS     251
#define DLE_TX_TIME_US    2120
#define COMPANY_ID_DEV    0xFFFF // docs/06 §1: development company id
#define RX_BUF_LEN        ATT_MTU_MAX

// 7a3e000X-5a1b-4c8e-9f2d-3b6c1e0d4a77, little endian.
#define S3W_UUID(x) \
    BLE_UUID128_INIT(0x77, 0x4a, 0x0d, 0x1e, 0x6c, 0x3b, 0x2d, 0x9f, 0x8e, 0x4c, 0x1b, 0x5a, (x), 0x00, 0x3e, 0x7a)

static const ble_uuid128_t k_uuid_svc = S3W_UUID(0x01);
static const ble_uuid128_t k_uuid_rx = S3W_UUID(0x02);
static const ble_uuid128_t k_uuid_tx = S3W_UUID(0x03);
static const ble_uuid128_t k_uuid_bulk = S3W_UUID(0x04);

typedef struct {
    uint16_t handle; // CONN_NONE = free
    int64_t t0_us;
    uint16_t mtu;
} link_t;

static struct {
    portMUX_TYPE lock; // guards what the API reads (status, rx callback)
    bool started;
    bool synced;
    bool enabled;        // host task (status reads it under the lock)
    bool want_enabled;   // set by svc_ble_set_enabled(), applied on the host task
    uint8_t own_addr_type;
    char name[16];
    link_t links[MAX_LINKS];
    uint16_t companion;  // secured companion link
    uint16_t pair_conn;  // numeric comparison in progress on this link
    bool tx_sub;
    bool bulk_sub;
    bool adv_fast;
    bool paired;
    ble_addr_t peer;     // companion identity (paired)
    svc_ble_rx_cb_t rx_cb;
    void *rx_ctx;
    uint32_t connects, pairings, rx_frames, tx_frames;
    svc_ble_state_t last_state;
    uint8_t last_links;
    bool last_paired;
    bool posted_once;
    struct ble_npl_event ev_forget;
    struct ble_npl_event ev_enable;
    struct ble_npl_event ev_reconnect;
} s = {
    .lock = portMUX_INITIALIZER_UNLOCKED,
    .companion = CONN_NONE,
    .pair_conn = CONN_NONE,
};

static uint16_t s_tx_handle;
static uint16_t s_bulk_handle;
static uint8_t s_rx_buf[RX_BUF_LEN]; // host task only

static void adv_start(bool fast);
static int gap_event(struct ble_gap_event *event, void *arg);

// --- State ------------------------------------------------------------------------------

static void post(svc_ble_event_t id, const void *data, size_t len)
{
    if (s3w_event_post(SVC_BLE_EVENT, id, data, len) != ESP_OK) {
        ESP_LOGW(TAG, "event %d dropped", (int)id);
    }
}

static uint8_t link_count(void)
{
    uint8_t n = 0;
    for (int i = 0; i < MAX_LINKS; i++) {
        n += s.links[i].handle != CONN_NONE;
    }
    return n;
}

static link_t *link_find(uint16_t conn)
{
    for (int i = 0; i < MAX_LINKS; i++) {
        if (s.links[i].handle == conn) {
            return &s.links[i];
        }
    }
    return NULL;
}

static svc_ble_state_t state_now(void)
{
    if (!s.synced || !s.enabled) {
        return SVC_BLE_STATE_OFF;
    }
    if (s.companion != CONN_NONE) {
        return SVC_BLE_STATE_SECURED;
    }
    if (link_count() > 0) {
        return SVC_BLE_STATE_CONNECTED;
    }
    return ble_gap_adv_active() ? SVC_BLE_STATE_ADVERTISING : SVC_BLE_STATE_IDLE;
}

// Host task. The bond store is the truth for "paired"; read it after every change.
static void refresh_paired(void)
{
    ble_addr_t peers[MYNEWT_VAL(BLE_STORE_MAX_BONDS)];
    int n = 0;
    if (ble_store_util_bonded_peers(peers, &n, MYNEWT_VAL(BLE_STORE_MAX_BONDS)) != 0) {
        n = 0;
    }
    portENTER_CRITICAL(&s.lock);
    s.paired = n > 0;
    if (n > 0) {
        s.peer = peers[0];
    }
    portEXIT_CRITICAL(&s.lock);
}

// Host task: publish the state if it changed.
static void post_state(void)
{
    const svc_ble_evt_state_t e = {.state = (uint8_t)state_now(), .paired = s.paired, .links = link_count()};
    if (s.posted_once && e.state == s.last_state && e.paired == s.last_paired && e.links == s.last_links) {
        return;
    }
    s.posted_once = true;
    s.last_state = (svc_ble_state_t)e.state;
    s.last_paired = e.paired;
    s.last_links = e.links;
    ESP_LOGI(TAG, "%s, %s, %u link(s)", svc_ble_state_name((svc_ble_state_t)e.state), e.paired ? "paired" : "not paired",
             e.links);
    post(SVC_BLE_EVT_STATE, &e, sizeof e);
}

// Host task: advertise unless Bluetooth is off, the companion is here or every link slot is taken.
static void adv_update(bool fast)
{
    const bool want = s.synced && s.enabled && s.companion == CONN_NONE && link_count() < MAX_LINKS;
    if (!want) {
        if (ble_gap_adv_active()) {
            ble_gap_adv_stop();
        }
    } else {
        adv_start(fast);
    }
    post_state();
}

// --- Advertising ------------------------------------------------------------------------

static void adv_start(bool fast)
{
    if (ble_gap_adv_active()) {
        ble_gap_adv_stop();
    }
    // Advertising data: flags, name, manufacturer data {company, model, flags} (22 of 31 bytes).
    const uint8_t mfg[] = {COMPANY_ID_DEV & 0xFF, COMPANY_ID_DEV >> 8, SVC_BLE_MODEL_ID,
                           s.paired ? SVC_BLE_ADV_F_PAIRED : 0};
    struct ble_hs_adv_fields fields = {
        .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .name = (const uint8_t *)s.name,
        .name_len = (uint8_t)strlen(s.name),
        .name_is_complete = 1,
        .mfg_data = mfg,
        .mfg_data_len = sizeof mfg,
    };
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc == 0) {
        // Scan response: the S3W Link service UUID (CompanionDeviceManager filter).
        const struct ble_hs_adv_fields rsp = {
            .uuids128 = &k_uuid_svc,
            .num_uuids128 = 1,
            .uuids128_is_complete = 1,
        };
        rc = ble_gap_adv_rsp_set_fields(&rsp);
    }
    const uint16_t itvl = fast ? BLE_GAP_ADV_ITVL_MS(ADV_FAST_MS) : BLE_GAP_ADV_ITVL_MS(ADV_SLOW_MS);
    const struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = itvl,
        .itvl_max = itvl,
    };
    if (rc == 0) {
        rc = ble_gap_adv_start(s.own_addr_type, NULL, fast ? ADV_FAST_TIME_MS : BLE_HS_FOREVER, &params, gap_event,
                               NULL);
    }
    if (rc != 0) {
        ESP_LOGW(TAG, "advertising: rc=%d", rc);
        return;
    }
    s.adv_fast = fast;
}

// --- Bonds ------------------------------------------------------------------------------

// Host task. Keep only the bond of keep (NULL: delete all).
static void delete_bonds_except(const ble_addr_t *keep)
{
    ble_addr_t peers[MYNEWT_VAL(BLE_STORE_MAX_BONDS)];
    int n = 0;
    if (ble_store_util_bonded_peers(peers, &n, MYNEWT_VAL(BLE_STORE_MAX_BONDS)) != 0) {
        return;
    }
    for (int i = 0; i < n; i++) {
        if (keep == NULL || ble_addr_cmp(&peers[i], keep) != 0) {
            ESP_LOGI(TAG, "deleting bond %02x:%02x:%02x:%02x:%02x:%02x", peers[i].val[5], peers[i].val[4],
                     peers[i].val[3], peers[i].val[2], peers[i].val[1], peers[i].val[0]);
            ble_store_util_delete_peer(&peers[i]);
        }
    }
}

static void pair_done(uint16_t conn, bool ok, int status)
{
    if (s.pair_conn != conn) {
        return;
    }
    s.pair_conn = CONN_NONE;
    const svc_ble_evt_pair_done_t e = {.conn = conn, .ok = ok, .status = status};
    ESP_LOGI(TAG, "pairing %s (status %d)", ok ? "done" : "failed", status);
    post(SVC_BLE_EVT_PAIR_DONE, &e, sizeof e);
}

// --- GAP --------------------------------------------------------------------------------

static void on_connect(uint16_t conn)
{
    if (!s.enabled) { // connected while advertising was being stopped
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }
    link_t *l = link_find(CONN_NONE);
    if (l == NULL) { // the controller allows more links than we use
        ble_gap_terminate(conn, BLE_ERR_CONN_LIMIT);
        return;
    }
    portENTER_CRITICAL(&s.lock);
    *l = (link_t){.handle = conn, .t0_us = esp_timer_get_time(), .mtu = ATT_MTU_DEFAULT};
    s.connects++;
    portEXIT_CRITICAL(&s.lock);

    // docs/06 §1: Data Length Extension 251 and 2M PHY when the phone supports them.
    ble_gap_set_data_len(conn, DLE_TX_OCTETS, DLE_TX_TIME_US);
    ble_gap_set_prefered_le_phy(conn, BLE_GAP_LE_PHY_1M_MASK | BLE_GAP_LE_PHY_2M_MASK,
                                BLE_GAP_LE_PHY_1M_MASK | BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_CODED_ANY);

    // The watch never starts security: the phone encrypts (or pairs) on its first access to a
    // protected characteristic. A Security Request from us to a phone that dropped its half of
    // the bond (re-pairing after an app reinstall) makes Android show an extra consent prompt
    // before its own numeric comparison.
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find(conn, &d) == 0) {
        ESP_LOGI(TAG, "connected %02x:%02x:%02x:%02x:%02x:%02x (handle %u)", d.peer_id_addr.val[5],
                 d.peer_id_addr.val[4], d.peer_id_addr.val[3], d.peer_id_addr.val[2], d.peer_id_addr.val[1],
                 d.peer_id_addr.val[0], conn);
    }
    adv_update(false); // a second slot stays open for the companion
}

static void on_disconnect(const struct ble_gap_event *event)
{
    const uint16_t conn = event->disconnect.conn.conn_handle;
    link_t *l = link_find(conn);
    svc_ble_evt_disconnected_t e = {.reason = (uint16_t)event->disconnect.reason, .companion = conn == s.companion};
    portENTER_CRITICAL(&s.lock);
    if (l) {
        e.seconds = (uint32_t)((esp_timer_get_time() - l->t0_us) / 1000000);
        l->handle = CONN_NONE;
    }
    if (conn == s.companion) {
        s.companion = CONN_NONE;
        s.tx_sub = false;
        s.bulk_sub = false;
    }
    portEXIT_CRITICAL(&s.lock);
    ESP_LOGI(TAG, "disconnected (handle %u, reason 0x%x, %lu s)", conn, e.reason, (unsigned long)e.seconds);
    pair_done(conn, false, BLE_HS_ENOTCONN);
    post(SVC_BLE_EVT_DISCONNECTED, &e, sizeof e);
    adv_update(true);
}

static void on_enc_change(uint16_t conn, int status)
{
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find(conn, &d) != 0) {
        return;
    }
    if (status != 0) {
        // Rejected or timed-out pairing, or a phone that lost its keys: it pairs again.
        ESP_LOGW(TAG, "encryption failed on %u: %d", conn, status);
        pair_done(conn, false, status);
        return;
    }
    if (!d.sec_state.authenticated || !d.sec_state.bonded) {
        // Just Works or no bonding: not allowed for the companion (docs/06 §8).
        ESP_LOGW(TAG, "link %u: %s, %s: dropping", conn, d.sec_state.authenticated ? "authenticated" : "no MITM",
                 d.sec_state.bonded ? "bonded" : "not bonded");
        if (d.sec_state.bonded) {
            ble_store_util_delete_peer(&d.peer_id_addr);
        }
        pair_done(conn, false, BLE_HS_EAUTHEN);
        ble_gap_terminate(conn, BLE_ERR_AUTH_FAIL);
        refresh_paired();
        return;
    }
    if (s.pair_conn == conn) {
        delete_bonds_except(&d.peer_id_addr); // single companion
        s.pairings++;
    } else if (s.companion != CONN_NONE && s.companion != conn) {
        // A second bonded link (left over from before a forget race): keep the first.
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }
    portENTER_CRITICAL(&s.lock);
    s.companion = conn;
    portEXIT_CRITICAL(&s.lock);
    refresh_paired();
    pair_done(conn, true, 0);
    adv_update(false); // stops advertising
}

static void on_passkey(uint16_t conn, const struct ble_gap_passkey_params *p)
{
    if (p->action != BLE_SM_IOACT_NUMCMP) {
        // Only numeric comparison is allowed (docs/06 §8); a phone without a display
        // would need Just Works or passkey entry.
        ESP_LOGW(TAG, "pairing action %u not supported: dropping link %u", p->action, conn);
        ble_gap_terminate(conn, BLE_ERR_AUTH_FAIL);
        return;
    }
    struct ble_gap_conn_desc d;
    bool replaces = false;
    if (ble_gap_conn_find(conn, &d) == 0) {
        ble_addr_t peers[MYNEWT_VAL(BLE_STORE_MAX_BONDS)];
        int n = 0;
        if (ble_store_util_bonded_peers(peers, &n, MYNEWT_VAL(BLE_STORE_MAX_BONDS)) == 0) {
            for (int i = 0; i < n; i++) {
                replaces |= ble_addr_cmp(&peers[i], &d.peer_id_addr) != 0;
            }
        }
    }
    s.pair_conn = conn;
    const svc_ble_evt_pair_request_t e = {.conn = conn, .passkey = p->numcmp, .replaces = replaces};
    ESP_LOGI(TAG, "pairing request on %u: code %06lu%s", conn, (unsigned long)p->numcmp,
             replaces ? " (replaces the paired phone)" : "");
    post(SVC_BLE_EVT_PAIR_REQUEST, &e, sizeof e);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            on_connect(event->connect.conn_handle);
        } else {
            adv_update(true);
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        on_disconnect(event);
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        // The fast period ended (or a link took it): go on slowly if still wanted.
        if (s.enabled && link_count() < MAX_LINKS && s.companion == CONN_NONE && !ble_gap_adv_active()) {
            adv_start(false);
        }
        post_state();
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        on_enc_change(event->enc_change.conn_handle, event->enc_change.status);
        return 0;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        on_passkey(event->passkey.conn_handle, &event->passkey.params);
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // The phone lost its bond and pairs again: drop the old keys and let it. The
        // user still confirms the code on the watch.
        struct ble_gap_conn_desc d;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &d) == 0) {
            ble_store_util_delete_peer(&d.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.conn_handle == s.companion) {
            portENTER_CRITICAL(&s.lock);
            if (event->subscribe.attr_handle == s_tx_handle) {
                s.tx_sub = event->subscribe.cur_notify;
            } else if (event->subscribe.attr_handle == s_bulk_handle) {
                s.bulk_sub = event->subscribe.cur_notify;
            }
            portEXIT_CRITICAL(&s.lock);
        }
        return 0;
    case BLE_GAP_EVENT_MTU: {
        link_t *l = link_find(event->mtu.conn_handle);
        if (l) {
            portENTER_CRITICAL(&s.lock);
            l->mtu = event->mtu.value;
            portEXIT_CRITICAL(&s.lock);
        }
        ESP_LOGI(TAG, "MTU %u on %u", event->mtu.value, event->mtu.conn_handle);
        return 0;
    }
    case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        ESP_LOGD(TAG, "PHY tx %u rx %u on %u", event->phy_updated.tx_phy, event->phy_updated.rx_phy,
                 event->phy_updated.conn_handle);
        return 0;
    default:
        return 0;
    }
}

// --- GATT: S3W Link ---------------------------------------------------------------------

static int link_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    // The flags already demand an authenticated, encrypted link; it must also be the
    // bonded companion (docs/06 §8: no S3W writes from unbonded links).
    if (conn != s.companion) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, s_rx_buf, sizeof s_rx_buf, &len) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    portENTER_CRITICAL(&s.lock);
    const svc_ble_rx_cb_t cb = s.rx_cb;
    void *cb_ctx = s.rx_ctx;
    s.rx_frames++;
    portEXIT_CRITICAL(&s.lock);
    if (cb) {
        cb((svc_ble_chan_t)(uintptr_t)arg, s_rx_buf, len, cb_ctx);
    }
    return 0;
}

static int tx_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn;
    (void)attr;
    (void)ctxt;
    (void)arg;
    return BLE_ATT_ERR_UNLIKELY; // notify only
}

#define SECURE_WRITE (BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN)
#define SECURE_NOTIFY \
    (BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC | BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHEN)

static const struct ble_gatt_svc_def k_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &k_uuid_svc.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &k_uuid_rx.u,
                    .access_cb = link_access,
                    .arg = (void *)(uintptr_t)SVC_BLE_CHAN_CONTROL,
                    .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | SECURE_WRITE,
                },
                {
                    .uuid = &k_uuid_tx.u,
                    .access_cb = tx_access,
                    .flags = SECURE_NOTIFY,
                    .val_handle = &s_tx_handle,
                },
                {
                    .uuid = &k_uuid_bulk.u,
                    .access_cb = link_access,
                    .arg = (void *)(uintptr_t)SVC_BLE_CHAN_BULK,
                    .flags = BLE_GATT_CHR_F_WRITE_NO_RSP | SECURE_WRITE | SECURE_NOTIFY,
                    .val_handle = &s_bulk_handle,
                },
                {0},
            },
    },
    {0},
};

// --- Device Information, Battery ------------------------------------------------------------

static char s_dis_fw[32];
static char s_dis_hw[24];
static char s_dis_serial[16];

static void dis_fill(void)
{
    snprintf(s_dis_fw, sizeof s_dis_fw, "%s", esp_app_get_description()->version);
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    snprintf(s_dis_hw, sizeof s_dis_hw, "ESP32-S3 v%d.%d", chip.revision / 100, chip.revision % 100);
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BASE);
    snprintf(s_dis_serial, sizeof s_dis_serial, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
    ble_svc_dis_manufacturer_name_set("S3Wear");
    ble_svc_dis_model_number_set("S3Wear-206");
    ble_svc_dis_firmware_revision_set(s_dis_fw);
    ble_svc_dis_hardware_revision_set(s_dis_hw);
    ble_svc_dis_serial_number_set(s_dis_serial);
}

static void set_battery(int8_t percent)
{
    if (percent >= 0) {
        ble_svc_bas_battery_level_set((uint8_t)(percent > 100 ? 100 : percent)); // notifies subscribers
    }
}

static void on_battery(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    (void)id;
    if (len >= sizeof(svc_power_battery_t)) {
        set_battery(((const svc_power_battery_t *)data)->percent);
    }
}

// --- Host -------------------------------------------------------------------------------

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) {
        rc = ble_hs_id_infer_auto(0, &s.own_addr_type);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "no BLE address: rc=%d", rc);
        return;
    }
    s.synced = true;
    refresh_paired();
    uint8_t addr[6] = {0};
    ble_hs_id_copy_addr(s.own_addr_type, addr, NULL);
    ESP_LOGI(TAG, "%s up, %02x:%02x:%02x:%02x:%02x:%02x, %s", s.name, addr[5], addr[4], addr[3], addr[2], addr[1],
             addr[0], s.paired ? "paired" : "not paired");
    adv_update(true);
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "host reset: %d", reason);
    portENTER_CRITICAL(&s.lock);
    s.synced = false;
    s.companion = CONN_NONE;
    s.tx_sub = false;
    s.bulk_sub = false;
    for (int i = 0; i < MAX_LINKS; i++) {
        s.links[i].handle = CONN_NONE;
    }
    portEXIT_CRITICAL(&s.lock);
    pair_done(s.pair_conn, false, reason);
    post_state();
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run(); // until nimble_port_stop()
    nimble_port_freertos_deinit();
}

// Host-task side of svc_ble_forget().
static void drop_links(void);

static void do_forget(struct ble_npl_event *ev)
{
    (void)ev;
    drop_links();
    delete_bonds_except(NULL);
    refresh_paired();
    ESP_LOGI(TAG, "bonds deleted");
    adv_update(true); // new manufacturer flags; links come back through DISCONNECT
}

static void drop_links(void)
{
    for (int i = 0; i < MAX_LINKS; i++) {
        if (s.links[i].handle != CONN_NONE) {
            ble_gap_terminate(s.links[i].handle, BLE_ERR_REM_USER_CONN_TERM);
        }
    }
}

// Host-task side of svc_ble_set_enabled().
static void do_enable(struct ble_npl_event *ev)
{
    (void)ev;
    portENTER_CRITICAL(&s.lock);
    const bool on = s.want_enabled;
    const bool changed = on != s.enabled;
    s.enabled = on;
    portEXIT_CRITICAL(&s.lock);
    if (!changed) {
        return;
    }
    ESP_LOGI(TAG, "Bluetooth %s", on ? "on" : "off");
    if (!on) {
        drop_links(); // the slots free up through DISCONNECT
    }
    adv_update(true); // off: stops advertising; on: fast advertising so the phone is back soon
}

// Host-task side of svc_ble_reconnect(). The phone is the central: all the watch can do is
// be easy to find again, with fast advertising for ADV_FAST_TIME_MS.
static void do_reconnect(struct ble_npl_event *ev)
{
    (void)ev;
    if (!s.synced || !s.enabled || s.companion != CONN_NONE) {
        return;
    }
    ESP_LOGI(TAG, "reconnect: fast advertising");
    adv_update(true);
}

// Bus task: follow the BLUETOOTH setting.
static void on_setting(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    const bool ours = id == SVC_SETTINGS_EVT_RESET ||
                      (id == SVC_SETTINGS_EVT_CHANGED && len >= sizeof(svc_settings_evt_changed_t) &&
                       ((const svc_settings_evt_changed_t *)data)->id == S3W_SETTING_BLUETOOTH);
    if (ours) {
        svc_ble_set_enabled(svc_settings_get_bool(S3W_SETTING_BLUETOOTH));
    }
}

esp_err_t svc_ble_start(void)
{
    ESP_RETURN_ON_FALSE(!s.started, ESP_ERR_INVALID_STATE, TAG, "already started");
    for (int i = 0; i < MAX_LINKS; i++) {
        s.links[i].handle = CONN_NONE;
    }
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(s.name, sizeof s.name, "S3Wear-%02X%02X", mac[4], mac[5]);

    ESP_RETURN_ON_ERROR(nimble_port_init(), TAG, "nimble init");

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    // LE Secure Connections, numeric comparison (watch shows the code, user confirms), bonding.
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_YESNO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    dis_fill();
    ble_svc_dis_init();
    ble_svc_bas_init();
    int rc = ble_gatts_count_cfg(k_services);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(k_services);
    }
    ESP_RETURN_ON_FALSE(rc == 0, ESP_FAIL, TAG, "GATT services: rc=%d", rc);
    ble_svc_gap_device_name_set(s.name);
    ble_att_set_preferred_mtu(ATT_MTU_MAX);
    ble_store_config_init();

    ble_npl_event_init(&s.ev_forget, do_forget, NULL);
    ble_npl_event_init(&s.ev_enable, do_enable, NULL);
    ble_npl_event_init(&s.ev_reconnect, do_reconnect, NULL);
    s.enabled = s.want_enabled = svc_settings_get_bool(S3W_SETTING_BLUETOOTH);
    ESP_RETURN_ON_ERROR(s3w_event_subscribe(SVC_SETTINGS_EVENT, ESP_EVENT_ANY_ID, on_setting, NULL, NULL), TAG,
                        "settings");

    svc_power_battery_t bat = {.percent = -1};
    if (svc_power_battery(&bat) == ESP_OK) {
        set_battery(bat.percent);
    }
    ESP_RETURN_ON_ERROR(s3w_event_subscribe(SVC_POWER_EVENT, SVC_POWER_EVT_BATTERY, on_battery, NULL, NULL), TAG,
                        "battery");

    s.started = true;
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

// --- API --------------------------------------------------------------------------------

esp_err_t svc_ble_pair_reply(uint16_t conn, bool accept)
{
    ESP_RETURN_ON_FALSE(s.started, ESP_ERR_INVALID_STATE, TAG, "not started");
    struct ble_sm_io io = {.action = BLE_SM_IOACT_NUMCMP, .numcmp_accept = accept ? 1 : 0};
    const int rc = ble_sm_inject_io(conn, &io);
    ESP_RETURN_ON_FALSE(rc == 0, ESP_ERR_INVALID_STATE, TAG, "pair reply on %u: rc=%d", conn, rc);
    ESP_LOGI(TAG, "pairing code %s", accept ? "confirmed" : "rejected");
    return ESP_OK;
}

esp_err_t svc_ble_forget(void)
{
    ESP_RETURN_ON_FALSE(s.started, ESP_ERR_INVALID_STATE, TAG, "not started");
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s.ev_forget);
    return ESP_OK;
}

esp_err_t svc_ble_set_enabled(bool on)
{
    ESP_RETURN_ON_FALSE(s.started, ESP_ERR_INVALID_STATE, TAG, "not started");
    portENTER_CRITICAL(&s.lock);
    s.want_enabled = on;
    portEXIT_CRITICAL(&s.lock);
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s.ev_enable);
    return ESP_OK;
}

esp_err_t svc_ble_reconnect(void)
{
    ESP_RETURN_ON_FALSE(s.started, ESP_ERR_INVALID_STATE, TAG, "not started");
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s.ev_reconnect);
    return ESP_OK;
}

void svc_ble_set_rx(svc_ble_rx_cb_t cb, void *ctx)
{
    portENTER_CRITICAL(&s.lock);
    s.rx_cb = cb;
    s.rx_ctx = ctx;
    portEXIT_CRITICAL(&s.lock);
}

esp_err_t svc_ble_send(svc_ble_chan_t chan, const uint8_t *data, size_t len)
{
    portENTER_CRITICAL(&s.lock);
    const uint16_t conn = s.companion;
    const bool sub = chan == SVC_BLE_CHAN_BULK ? s.bulk_sub : s.tx_sub;
    link_t *l = link_find(conn);
    const uint16_t mtu = l ? l->mtu : ATT_MTU_DEFAULT;
    portEXIT_CRITICAL(&s.lock);
    if (conn == CONN_NONE || !sub) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_FALSE(len <= (size_t)(mtu - 3), ESP_ERR_INVALID_SIZE, TAG, "frame %u > MTU %u - 3", (unsigned)len,
                        mtu);
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
    if (om == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const int rc = ble_gatts_notify_custom(conn, chan == SVC_BLE_CHAN_BULK ? s_bulk_handle : s_tx_handle, om);
    if (rc != 0) {
        return rc == BLE_HS_ENOMEM ? ESP_ERR_NO_MEM : ESP_FAIL;
    }
    portENTER_CRITICAL(&s.lock);
    s.tx_frames++;
    portEXIT_CRITICAL(&s.lock);
    return ESP_OK;
}

uint16_t svc_ble_mtu(void)
{
    portENTER_CRITICAL(&s.lock);
    const link_t *l = link_find(s.companion);
    const uint16_t mtu = l ? l->mtu : ATT_MTU_DEFAULT;
    portEXIT_CRITICAL(&s.lock);
    return mtu;
}

void svc_ble_get_status(svc_ble_status_t *out)
{
    memset(out, 0, sizeof *out);
    out->conn = CONN_NONE;
    out->pair_conn = CONN_NONE;
    out->mtu = ATT_MTU_DEFAULT;
    if (!s.started) {
        return;
    }
    if (s.synced) {
        ble_hs_id_copy_addr(s.own_addr_type, out->addr, NULL);
    }
    const bool adv = s.synced && ble_gap_adv_active();
    portENTER_CRITICAL(&s.lock);
    out->enabled = s.enabled;
    out->links = link_count();
    out->state = !s.synced || !s.enabled       ? SVC_BLE_STATE_OFF
                 : s.companion != CONN_NONE    ? SVC_BLE_STATE_SECURED
                 : out->links > 0              ? SVC_BLE_STATE_CONNECTED
                 : adv                         ? SVC_BLE_STATE_ADVERTISING
                                               : SVC_BLE_STATE_IDLE;
    out->paired = s.paired;
    memcpy(out->name, s.name, sizeof out->name);
    out->addr_type = s.own_addr_type;
    memcpy(out->peer, s.peer.val, sizeof out->peer);
    out->peer_type = s.peer.type;
    out->conn = s.companion;
    out->pair_conn = s.pair_conn;
    const link_t *l = link_find(s.companion);
    out->mtu = l ? l->mtu : ATT_MTU_DEFAULT;
    out->tx_subscribed = s.tx_sub;
    out->bulk_subscribed = s.bulk_sub;
    out->adv_fast = adv && s.adv_fast;
    out->connects = s.connects;
    out->pairings = s.pairings;
    out->rx_frames = s.rx_frames;
    out->tx_frames = s.tx_frames;
    portEXIT_CRITICAL(&s.lock);
}

const char *svc_ble_state_name(svc_ble_state_t state)
{
    static const char *const k_names[] = {
        [SVC_BLE_STATE_OFF] = "off",
        [SVC_BLE_STATE_IDLE] = "idle",
        [SVC_BLE_STATE_ADVERTISING] = "advertising",
        [SVC_BLE_STATE_CONNECTED] = "connected",
        [SVC_BLE_STATE_SECURED] = "secured",
    };
    return (unsigned)state < sizeof k_names / sizeof k_names[0] ? k_names[state] : "?";
}
