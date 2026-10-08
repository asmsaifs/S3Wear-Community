// Wi-Fi station (svc_wifi.h). The radio runs on svc_worker: settings changes, IDF events (copied
// from the default loop), the retry timer and API calls all queue jobs there, so the state below
// marked "worker" needs no lock. The saved list and the status snapshot are under s.lock, never
// held across a driver call or an event post.
#include "svc_wifi.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "aes/esp_aes.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "s3w_event.h"
#include "s3w_task.h"
#include "svc_settings.h"
#include "svc_time.h"
#include "svc_worker.h"
#include "wifi_priv.h"

static const char *TAG = "svc_wifi";

ESP_EVENT_DEFINE_BASE(SVC_WIFI_EVENT);

#define NVS_NS  "s3w_wifi"
#define NVS_KEY "nets"

#define SCAN_MAX     16    // access points read from a scan (strongest first)
#define JOIN_TIMEOUT_US (20 * 1000 * 1000) // association + DHCP; then the attempt fails
#define SNTP_SERVER  "pool.ntp.org"
// To start the driver (radio_up), after the RAM hook. The driver with sdkconfig.defaults' buffers
// takes ~33 KB internal (measured: ~69 KB free before, ~36 KB after, not joined).
#define MIN_INTERNAL_FREE (40 * 1024)

// IDF events copied to the worker.
typedef enum {
    EV_STA_START,
    EV_SCAN_DONE,
    EV_STA_CONNECTED,
    EV_STA_DISCONNECTED,
    EV_GOT_IP,
    EV_LOST_IP,
} ev_kind_t;

typedef struct {
    uint32_t gen; // radio session the event belongs to
    uint8_t kind; // ev_kind_t
    uint8_t reason; // EV_STA_DISCONNECTED: wifi_err_reason_t
    uint32_t ip;  // EV_GOT_IP
} ev_t;

typedef struct {
    int64_t utc_ms;
    int64_t at_us; // esp_timer time it arrived: the queue delay is added back
} sntp_job_t;

static struct {
    bool started;
    s3w_mutex_t lock;
    // Under lock.
    wifi_nets_t nets;
    svc_wifi_status_t st;
    int prefer; // index in nets of the network just added (joined first if in range), -1 = none
    // Worker.
    volatile uint32_t gen; // read by the event handler (any task), written on the worker
    bool radio;            // driver initialised and started
    bool scanning;
    bool associated;       // CONNECTING: joined the access point, waiting for DHCP
    bool sntp;
    svc_wifi_err_t timeout_err;
    uint32_t fails;     // attempts in a row that ended without an address
    uint32_t skip_mask; // saved networks refused this round (wrong password)
    esp_netif_t *netif;
    esp_timer_handle_t timer; // one-shot: join timeout (CONNECTING) or next scan (IDLE)
    esp_timer_handle_t idle_timer; // one-shot: WIFI_IDLE_OFF minutes without a holder -> off
    svc_wifi_ram_hook_t ram_hook;
} s = {.prefer = -1};

// --- Status ---------------------------------------------------------------------------------

// Under lock: the saved names into the snapshot.
static void saved_to_status(void)
{
    s.st.saved_count = s.nets.count;
    memset(s.st.saved, 0, sizeof s.st.saved);
    for (int i = 0; i < s.nets.count; i++) {
        strcpy(s.st.saved[i], s.nets.net[i].ssid);
    }
}

static void publish(void)
{
    s3w_event_post(SVC_WIFI_EVENT, SVC_WIFI_EVT_STATE, NULL, 0);
}

// Worker: a new state; ssid NULL keeps it, "" clears it.
static void set_state(svc_wifi_state_t state, const char *ssid)
{
    s3w_mutex_lock(s.lock, UINT32_MAX);
    s.st.state = state;
    if (ssid) {
        snprintf(s.st.ssid, sizeof s.st.ssid, "%s", ssid);
    }
    if (state != SVC_WIFI_CONNECTED) {
        s.st.ip = 0;
        s.st.rssi = 0;
    }
    s3w_mutex_unlock(s.lock);
    publish();
}

static void set_error(svc_wifi_err_t err, const char *ssid)
{
    s3w_mutex_lock(s.lock, UINT32_MAX);
    s.st.error = err;
    snprintf(s.st.error_ssid, sizeof s.st.error_ssid, "%s", ssid ? ssid : "");
    s3w_mutex_unlock(s.lock);
}

static svc_wifi_state_t state_now(void)
{
    s3w_mutex_lock(s.lock, UINT32_MAX);
    const svc_wifi_state_t st = s.st.state;
    s3w_mutex_unlock(s.lock);
    return st;
}

// --- Flash ----------------------------------------------------------------------------------

static void w_save(void *ctx)
{
    (void)ctx;
    static wifi_nets_t copy; // worker only; keeps the passwords off the stack
    s3w_mutex_lock(s.lock, UINT32_MAX);
    copy = s.nets;
    s3w_mutex_unlock(s.lock);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NVS_KEY, &copy, sizeof copy);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    memset(&copy, 0, sizeof copy);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save networks: %s", esp_err_to_name(err));
    }
}

static void load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return; // nothing saved yet
    }
    size_t len = sizeof s.nets;
    const esp_err_t err = nvs_get_blob(h, NVS_KEY, &s.nets, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof s.nets || !wifi_nets_valid(&s.nets)) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "saved networks unreadable (%s, %u bytes): none", esp_err_to_name(err), (unsigned)len);
        }
        memset(&s.nets, 0, sizeof s.nets);
    }
}

// --- SNTP -----------------------------------------------------------------------------------

static void w_sntp(const void *data, size_t len)
{
    if (len != sizeof(sntp_job_t)) {
        return;
    }
    const sntp_job_t *j = data;
    const int64_t ms = j->utc_ms + (esp_timer_get_time() - j->at_us) / 1000;
    const esp_err_t err = svc_time_set_utc_ms(ms, SVC_TIME_SRC_SNTP);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP time refused: %s", esp_err_to_name(err));
        return;
    }
    s3w_mutex_lock(s.lock, UINT32_MAX);
    s.st.sntp_syncs++;
    s.st.sntp_last_ms = ms;
    s3w_mutex_unlock(s.lock);
    ESP_LOGI(TAG, "clock set from %s", SNTP_SERVER);
    publish();
}

// Replaces lwIP's weak sntp_sync_time() (tcpip task): the clock is svc_time's to set, with drift
// calibration, so the time goes to the worker instead of settimeofday(). Nothing else in the
// firmware uses SNTP.
void sntp_sync_time(struct timeval *tv)
{
    sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
    const sntp_job_t j = {.utc_ms = (int64_t)tv->tv_sec * 1000 + tv->tv_usec / 1000, .at_us = esp_timer_get_time()};
    if (svc_worker_submit_copy(w_sntp, &j, sizeof j) != ESP_OK) {
        ESP_LOGW(TAG, "SNTP time dropped: worker busy");
    }
}

static void sntp_on(void)
{
    if (s.sntp) {
        esp_sntp_restart();
        return;
    }
    // Poll every CONFIG_LWIP_SNTP_UPDATE_DELAY (6 h, sdkconfig.defaults) while joined: the RTC
    // drifts < 1 s a day after calibration, and the phone syncs every 6 h too.
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, SNTP_SERVER);
    esp_sntp_init();
    s.sntp = true;
}

static void sntp_off(void)
{
    if (s.sntp) {
        esp_sntp_stop();
        s.sntp = false;
    }
}

// --- Idle switch-off (worker) ---------------------------------------------------------------

// Count WIFI_IDLE_OFF minutes from now, or stop counting (off, held, 0 = never).
static void idle_rearm(void)
{
    esp_timer_stop(s.idle_timer);
    s3w_mutex_lock(s.lock, UINT32_MAX);
    const bool held = s.st.holders > 0;
    s3w_mutex_unlock(s.lock);
    const int32_t min = svc_settings_get_int(S3W_SETTING_WIFI_IDLE_OFF);
    if (s.radio && !held && min > 0) {
        esp_timer_start_once(s.idle_timer, (uint64_t)min * 60 * 1000000);
    }
}

static void w_idle_rearm(void *ctx)
{
    (void)ctx;
    idle_rearm();
}

static void w_idle(void *ctx)
{
    (void)ctx;
    s3w_mutex_lock(s.lock, UINT32_MAX);
    const bool held = s.st.holders > 0;
    s3w_mutex_unlock(s.lock);
    const int32_t min = svc_settings_get_int(S3W_SETTING_WIFI_IDLE_OFF);
    if (!s.radio || held || min <= 0) {
        return;
    }
    ESP_LOGI(TAG, "unused for %ld min: off", (long)min);
    svc_wifi_set_on(false); // the setting's job turns the radio off
}

static void idle_cb(void *arg)
{
    (void)arg;
    svc_worker_submit(w_idle, NULL);
}

// --- Radio (worker) -------------------------------------------------------------------------

static void arm(uint64_t us)
{
    esp_timer_stop(s.timer);
    esp_timer_start_once(s.timer, us);
}

static void scan(void)
{
    if (!s.radio || s.scanning) {
        return;
    }
    const esp_err_t err = esp_wifi_scan_start(NULL, false); // SCAN_DONE follows
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan: %s", esp_err_to_name(err));
        s.fails++;
        arm((uint64_t)wifi_nets_backoff_ms(s.fails) * 1000);
        return;
    }
    s.scanning = true;
}

// Nothing joined this round: wait and scan again.
static void idle_retry(void)
{
    s.fails++;
    s.skip_mask = 0; // next round tries a refused password again (it may have been a glitch)
    const uint32_t ms = wifi_nets_backoff_ms(s.fails);
    ESP_LOGI(TAG, "not joined; scanning again in %lu s", (unsigned long)(ms / 1000));
    set_state(SVC_WIFI_IDLE, "");
    arm((uint64_t)ms * 1000);
}

static void join(const wifi_net_t *net)
{
    wifi_config_t wc = {0};
    memcpy(wc.sta.ssid, net->ssid, strlen(net->ssid));
    memcpy(wc.sta.password, net->pass, strlen(net->pass));
    // The weakest security accepted: WPA2 for a password (WPA3 and transition networks too),
    // anything for an open network.
    wc.sta.threshold.authmode = net->pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    wc.sta.pmf_cfg.capable = true;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (err == ESP_OK) {
        err = esp_wifi_connect();
    }
    memset(&wc, 0, sizeof wc);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "join \"%s\": %s", net->ssid, esp_err_to_name(err));
        set_error(SVC_WIFI_ERR_OTHER, net->ssid);
        idle_retry();
        return;
    }
    ESP_LOGI(TAG, "joining \"%s\"", net->ssid);
    s.associated = false;
    s.timeout_err = SVC_WIFI_ERR_NONE;
    set_state(SVC_WIFI_CONNECTING, net->ssid);
    arm(JOIN_TIMEOUT_US);
}

static void on_scan_done(void)
{
    s.scanning = false;
    uint16_t n = SCAN_MAX;
    wifi_ap_record_t *recs = calloc(n, sizeof *recs); // < 4 KB: internal is fine
    wifi_seen_t *seen = calloc(n, sizeof *seen);
    if (!recs || !seen || esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) {
        n = 0;
    }
    esp_wifi_clear_ap_list();
    for (int i = 0; i < n; i++) {
        snprintf(seen[i].ssid, sizeof seen[i].ssid, "%.*s", WIFI_SSID_MAX, (const char *)recs[i].ssid);
        seen[i].rssi = recs[i].rssi;
    }
    free(recs);
    if (!s.radio || state_now() != SVC_WIFI_IDLE) {
        free(seen); // joined or turned off meanwhile
        return;
    }
    static wifi_net_t net; // worker only
    s3w_mutex_lock(s.lock, UINT32_MAX);
    const int pick = seen ? wifi_nets_pick(&s.nets, seen, n, s.prefer, s.skip_mask) : -1;
    if (pick >= 0) {
        net = s.nets.net[pick];
    }
    s3w_mutex_unlock(s.lock);
    free(seen);
    ESP_LOGD(TAG, "scan: %u access points, pick %d", n, pick);
    if (pick < 0) {
        if (!s.skip_mask) {
            set_error(SVC_WIFI_ERR_NOT_FOUND, NULL); // a refused password keeps its error
        }
        idle_retry();
        return;
    }
    join(&net);
    memset(&net, 0, sizeof net);
}

static void on_got_ip(uint32_t ip)
{
    esp_timer_stop(s.timer);
    wifi_ap_record_t ap = {0};
    const bool have_ap = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    s.fails = 0;
    s.skip_mask = 0;
    s3w_mutex_lock(s.lock, UINT32_MAX);
    s.prefer = -1;
    s.st.state = SVC_WIFI_CONNECTED;
    s.st.ip = ip;
    s.st.rssi = have_ap ? ap.rssi : 0;
    s.st.error = SVC_WIFI_ERR_NONE;
    s.st.error_ssid[0] = '\0';
    s3w_mutex_unlock(s.lock);
    const esp_ip4_addr_t a = {.addr = ip};
    ESP_LOGI(TAG, "joined, address " IPSTR ", %d dBm", IP2STR(&a), have_ap ? ap.rssi : 0);
    publish();
    idle_rearm();
    sntp_on();
}

static svc_wifi_err_t reason_error(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
        return SVC_WIFI_ERR_AUTH;
    case WIFI_REASON_NO_AP_FOUND:
        return SVC_WIFI_ERR_NOT_FOUND;
    default:
        return SVC_WIFI_ERR_OTHER;
    }
}

static void on_disconnected(uint8_t reason)
{
    s.associated = false;
    s3w_mutex_lock(s.lock, UINT32_MAX);
    const svc_wifi_state_t was = s.st.state;
    char ssid[WIFI_SSID_MAX + 1];
    strcpy(ssid, s.st.ssid);
    const int idx = wifi_nets_find(&s.nets, ssid);
    s3w_mutex_unlock(s.lock);
    if (was == SVC_WIFI_CONNECTED) {
        ESP_LOGI(TAG, "left \"%s\" (reason %u)", ssid, reason);
        sntp_off();
        esp_timer_stop(s.timer);
        s.fails = 0;
        set_state(SVC_WIFI_IDLE, "");
        scan(); // back at once if it is still there
        return;
    }
    if (was != SVC_WIFI_CONNECTING) {
        return; // we let go of it (timeout, switch, a new network)
    }
    esp_timer_stop(s.timer);
    const svc_wifi_err_t err = reason_error(reason);
    ESP_LOGW(TAG, "\"%s\" failed (reason %u)", ssid, reason);
    set_error(err, err == SVC_WIFI_ERR_NOT_FOUND ? NULL : ssid);
    if (err == SVC_WIFI_ERR_AUTH && idx >= 0) {
        s.skip_mask |= 1u << idx; // try the other saved networks first
        set_state(SVC_WIFI_IDLE, "");
        scan();
        return;
    }
    idle_retry();
}

static void w_timer(void *ctx)
{
    if ((uint32_t)(uintptr_t)ctx != s.gen || !s.radio) {
        return;
    }
    s3w_mutex_lock(s.lock, UINT32_MAX);
    const svc_wifi_state_t st = s.st.state;
    char ssid[WIFI_SSID_MAX + 1];
    strcpy(ssid, s.st.ssid);
    s3w_mutex_unlock(s.lock);
    if (st == SVC_WIFI_CONNECTING) {
        ESP_LOGW(TAG, "\"%s\": %s in %d s", ssid, s.associated ? "no address" : "no answer", JOIN_TIMEOUT_US / 1000000);
        set_error(s.associated ? SVC_WIFI_ERR_NO_IP : SVC_WIFI_ERR_OTHER, ssid);
        s.associated = false;
        set_state(SVC_WIFI_IDLE, ""); // first: the DISCONNECTED that follows is then ignored
        esp_wifi_disconnect();
        idle_retry();
    } else if (st == SVC_WIFI_IDLE) {
        scan();
    }
}

static void timer_cb(void *arg)
{
    (void)arg;
    svc_worker_submit(w_timer, (void *)(uintptr_t)s.gen);
}

static void w_event(const void *data, size_t len)
{
    if (len != sizeof(ev_t)) {
        return;
    }
    const ev_t *e = data;
    if (e->gen != s.gen || !s.radio) {
        return; // an earlier radio session, or the factory test's own scan while ours is off
    }
    switch (e->kind) {
    case EV_STA_START:
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM); // DTIM modem sleep (also required next to BLE)
        scan();
        break;
    case EV_SCAN_DONE:
        on_scan_done();
        break;
    case EV_STA_CONNECTED:
        s.associated = true;
        break;
    case EV_STA_DISCONNECTED:
        on_disconnected(e->reason);
        break;
    case EV_GOT_IP:
        if (state_now() == SVC_WIFI_CONNECTING) {
            on_got_ip(e->ip);
        }
        break;
    case EV_LOST_IP:
        if (state_now() == SVC_WIFI_CONNECTED) {
            esp_wifi_disconnect(); // DISCONNECTED: scan and join again
        }
        break;
    default:
        break;
    }
}

// Default event loop task: copy to the worker.
static void on_idf_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    ev_t e = {.gen = s.gen};
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START: e.kind = EV_STA_START; break;
        case WIFI_EVENT_SCAN_DONE: e.kind = EV_SCAN_DONE; break;
        case WIFI_EVENT_STA_CONNECTED: e.kind = EV_STA_CONNECTED; break;
        case WIFI_EVENT_STA_DISCONNECTED:
            e.kind = EV_STA_DISCONNECTED;
            e.reason = ((const wifi_event_sta_disconnected_t *)data)->reason;
            break;
        default: return;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        e.kind = EV_GOT_IP;
        e.ip = ((const ip_event_got_ip_t *)data)->ip_info.ip.addr;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        e.kind = EV_LOST_IP;
    } else {
        return;
    }
    if (svc_worker_submit_copy(w_event, &e, sizeof e) != ESP_OK) {
        ESP_LOGW(TAG, "event %ld dropped: worker busy", (long)id);
    }
}

static esp_err_t radio_up(void)
{
    if (s.ram_hook) {
        s.ram_hook(true); // e.g. one LVGL draw buffer instead of two
    }
    // A failed esp_wifi_init() leaks what it got (measured ~6 KB internal) and can leave too little
    // for DMA; the netif's first start also creates lwIP's task. Refuse before either.
    const size_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (internal < MIN_INTERNAL_FREE) {
        ESP_LOGE(TAG, "only %u B internal RAM free", (unsigned)internal);
        if (s.ram_hook) {
            s.ram_hook(false);
        }
        return ESP_ERR_NO_MEM;
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init: %s", esp_err_to_name(err));
        if (s.ram_hook) {
            s.ram_hook(false);
        }
        return err;
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM); // the saved list is ours (NVS "s3w_wifi")
    if (err == ESP_OK) {
        err = esp_wifi_set_mode(WIFI_MODE_STA);
    }
    s.gen++;
    s.radio = true;
    s.scanning = false;
    s.fails = 0;
    s.skip_mask = 0;
    if (err == ESP_OK) {
        err = esp_wifi_start(); // STA_START: scan
    }
    if (err != ESP_OK) {
        s.radio = false;
        s.gen++;
        esp_wifi_deinit();
        if (s.ram_hook) {
            s.ram_hook(false);
        }
        ESP_LOGE(TAG, "start: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "on (%u B internal RAM free)", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    set_error(SVC_WIFI_ERR_NONE, NULL);
    set_state(SVC_WIFI_IDLE, "");
    idle_rearm();
    return ESP_OK;
}

static void radio_down(void)
{
    s.gen++;
    s.radio = false;
    s.scanning = false;
    s.associated = false;
    esp_timer_stop(s.timer);
    esp_timer_stop(s.idle_timer);
    sntp_off();
    esp_wifi_stop();
    esp_wifi_deinit(); // its internal RAM back
    // The driver's task stack is freed by the idle task once it gets to run: wait for it, or the
    // RAM hook finds the draw buffer's space still taken (measured: 6.5 KB right after it).
    vTaskDelay(pdMS_TO_TICKS(100));
    if (s.ram_hook) {
        s.ram_hook(false);
    }
    ESP_LOGI(TAG, "off");
    set_error(SVC_WIFI_ERR_NONE, NULL);
    set_state(SVC_WIFI_OFF, "");
}

// Worker: follow the WIFI setting.
static void w_apply(void *ctx)
{
    (void)ctx;
    const bool on = svc_settings_get_bool(S3W_SETTING_WIFI);
    if (on && !s.radio) {
        if (radio_up() != ESP_OK) {
            set_error(SVC_WIFI_ERR_OTHER, NULL);
            set_state(SVC_WIFI_OFF, "");
        }
    } else if (!on && s.radio) {
        radio_down();
    }
}

// Worker: the saved list changed (add / forget): drop a network no longer saved, try a new one.
static void w_nets_changed(void *ctx)
{
    const bool added = ctx != NULL;
    s.skip_mask = 0; // its bits are list indices, which moved
    if (!s.radio) {
        return;
    }
    if (added) {
        idle_rearm(); // the user just gave it a network: count from now
    }
    s3w_mutex_lock(s.lock, UINT32_MAX);
    const svc_wifi_state_t st = s.st.state;
    const bool saved = wifi_nets_find(&s.nets, s.st.ssid) >= 0;
    s3w_mutex_unlock(s.lock);
    const bool busy = st == SVC_WIFI_CONNECTING || st == SVC_WIFI_CONNECTED;
    if (busy && (!saved || added)) {
        // Forgotten, or a network was just added (another one, or this one with a new password):
        // leave this one and join from a fresh scan.
        esp_timer_stop(s.timer);
        s.associated = false;
        sntp_off();
        set_state(SVC_WIFI_IDLE, ""); // first: the DISCONNECTED that follows is then ignored
        esp_wifi_disconnect();
    } else if (busy || !added) {
        return;
    }
    esp_timer_stop(s.timer);
    s.fails = 0;
    scan();
}

// Bus task.
static void on_setting(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    const uint16_t which = id == SVC_SETTINGS_EVT_CHANGED && len >= sizeof(svc_settings_evt_changed_t)
                               ? ((const svc_settings_evt_changed_t *)data)->id
                               : UINT16_MAX;
    if (id == SVC_SETTINGS_EVT_RESET || which == S3W_SETTING_WIFI) {
        if (svc_worker_submit(w_apply, NULL) != ESP_OK) {
            ESP_LOGW(TAG, "switch change dropped: worker busy");
        }
    }
    if (which == S3W_SETTING_WIFI_IDLE_OFF) {
        svc_worker_submit(w_idle_rearm, NULL);
    }
}

// --- API ------------------------------------------------------------------------------------

esp_err_t svc_wifi_set_on(bool on)
{
    return svc_settings_set_bool(S3W_SETTING_WIFI, on);
}

esp_err_t svc_wifi_add(const char *ssid, const char *pass)
{
    ESP_RETURN_ON_FALSE(s.started, ESP_ERR_INVALID_STATE, TAG, "not started");
    s3w_mutex_lock(s.lock, UINT32_MAX);
    const wifi_nets_err_t e = wifi_nets_add(&s.nets, ssid, pass);
    if (e == WIFI_NETS_OK) {
        s.prefer = 0;
        s.st.error = SVC_WIFI_ERR_NONE; // the phone waits for this attempt's outcome
        s.st.error_ssid[0] = '\0';
        saved_to_status();
    }
    s3w_mutex_unlock(s.lock);
    if (e == WIFI_NETS_INVALID) {
        return ESP_ERR_INVALID_ARG;
    }
    if (e == WIFI_NETS_FULL) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "saved \"%s\"", ssid);
    svc_worker_submit(w_save, NULL);
    svc_worker_submit(w_nets_changed, (void *)1);
    publish();
    // On after the list is saved: the switch's job then finds the network.
    return svc_wifi_set_on(true);
}

esp_err_t svc_wifi_forget(const char *ssid)
{
    ESP_RETURN_ON_FALSE(s.started, ESP_ERR_INVALID_STATE, TAG, "not started");
    s3w_mutex_lock(s.lock, UINT32_MAX);
    const int at = ssid ? wifi_nets_find(&s.nets, ssid) : -1;
    const wifi_nets_err_t e = at < 0 ? WIFI_NETS_NOT_FOUND : wifi_nets_forget(&s.nets, ssid);
    if (e == WIFI_NETS_OK) {
        s.prefer = s.prefer == at ? -1 : s.prefer > at ? s.prefer - 1 : s.prefer;
        saved_to_status();
    }
    s3w_mutex_unlock(s.lock);
    if (e != WIFI_NETS_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "forgot \"%s\"", ssid);
    svc_worker_submit(w_save, NULL);
    svc_worker_submit(w_nets_changed, NULL);
    publish();
    return ESP_OK;
}

void svc_wifi_acquire(void)
{
    if (!s.started) {
        return;
    }
    s3w_mutex_lock(s.lock, UINT32_MAX);
    s.st.holders++;
    s3w_mutex_unlock(s.lock);
    svc_worker_submit(w_idle_rearm, NULL); // stops the count
}

void svc_wifi_release(void)
{
    if (!s.started) {
        return;
    }
    s3w_mutex_lock(s.lock, UINT32_MAX);
    if (s.st.holders > 0) {
        s.st.holders--;
    }
    s3w_mutex_unlock(s.lock);
    svc_worker_submit(w_idle_rearm, NULL); // the last one out starts it again
}

void svc_wifi_set_ram_hook(svc_wifi_ram_hook_t hook)
{
    s.ram_hook = hook;
}

static void w_sntp_sync(void *ctx)
{
    (void)ctx;
    if (s.sntp) {
        esp_sntp_restart();
    }
}

esp_err_t svc_wifi_sntp_sync(void)
{
    ESP_RETURN_ON_FALSE(s.started && state_now() == SVC_WIFI_CONNECTED, ESP_ERR_INVALID_STATE, TAG, "not joined");
    return svc_worker_submit(w_sntp_sync, NULL);
}

void svc_wifi_get(svc_wifi_status_t *out)
{
    if (!s.started) {
        memset(out, 0, sizeof *out);
        return;
    }
    s3w_mutex_lock(s.lock, UINT32_MAX);
    *out = s.st;
    s3w_mutex_unlock(s.lock);
    out->on = svc_settings_get_bool(S3W_SETTING_WIFI);
}

ESP_EVENT_DEFINE_BASE(SVC_WIFI_PREWARM_EVENT);

static esp_err_t on_prewarm_tcpip(void *ctx);

// Default event loop task: netif's Wi-Fi handlers call the driver and lwIP from it.
static void on_prewarm(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)base;
    (void)id;
    (void)data;
    esp_wifi_set_mode(WIFI_MODE_STA); // an API call that waits on this task's semaphore
    esp_netif_tcpip_exec(on_prewarm_tcpip, NULL); // lwIP's semaphore for this task
    xSemaphoreGive((SemaphoreHandle_t)arg);
}

// lwIP's task: esp_netif registers the driver's receive callback from it.
static esp_err_t on_prewarm_tcpip(void *ctx)
{
    (void)ctx;
    esp_wifi_set_mode(WIFI_MODE_STA); // an API call that waits on this task's semaphore
    return ESP_OK;
}

static esp_err_t netif_up(void)
{
    if (s.netif) {
        return ESP_OK;
    }
    // lwIP's task and the station netif live for good.
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    s.netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"); // the factory test may have made it
    if (!s.netif) {
        s.netif = esp_netif_create_default_wifi_sta();
    }
    ESP_RETURN_ON_FALSE(s.netif, ESP_FAIL, TAG, "sta netif");
    return ESP_OK;
}

// WPA2 uses the AES accelerator; its first DMA operation long enough for the interrupt (≥ 2000 B)
// allocates the interrupt and a lock for good.
static void prewarm_aes(void)
{
    const size_t len = 2048;
    uint8_t *buf = heap_caps_calloc(1, len, MALLOC_CAP_SPIRAM);
    if (!buf) {
        return;
    }
    static const uint8_t key[16];
    uint8_t iv[16] = {0};
    esp_aes_context ctx;
    esp_aes_init(&ctx);
    if (esp_aes_setkey(&ctx, key, 128) == 0) {
        esp_aes_crypt_cbc(&ctx, ESP_AES_ENCRYPT, len, iv, buf, buf);
    }
    esp_aes_free(&ctx);
    heap_caps_free(buf);
}

static void w_prewarm(void *ctx)
{
    prewarm_aes();
    esp_err_t err = netif_up();
    if (err == ESP_OK) {
        // Goes through lwIP's API call: the worker's lwIP semaphore (SNTP is set up from here).
        esp_sntp_setservername(0, SNTP_SERVER);
    }
    const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (err == ESP_OK) {
        err = esp_wifi_init(&cfg);
    }
    if (err == ESP_OK) {
        esp_wifi_set_mode(WIFI_MODE_STA); // an API call that waits on the worker's semaphore
        esp_netif_tcpip_exec(on_prewarm_tcpip, NULL);
        SemaphoreHandle_t done = xSemaphoreCreateBinary();
        esp_event_handler_instance_t h = NULL;
        if (done && esp_event_handler_instance_register(SVC_WIFI_PREWARM_EVENT, 0, on_prewarm, done, &h) == ESP_OK) {
            if (esp_event_post(SVC_WIFI_PREWARM_EVENT, 0, NULL, 0, pdMS_TO_TICKS(100)) == ESP_OK) {
                xSemaphoreTake(done, pdMS_TO_TICKS(1000));
            }
            esp_event_handler_instance_unregister(SVC_WIFI_PREWARM_EVENT, 0, h);
        }
        if (done) {
            vSemaphoreDelete(done);
        }
        err = esp_wifi_deinit();
    }
    *(esp_err_t *)ctx = err;
}

static void w_done(void *ctx)
{
    xSemaphoreGive((SemaphoreHandle_t)ctx);
}

esp_err_t svc_wifi_prewarm(void)
{
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(done, ESP_ERR_NO_MEM, TAG, "sem");
    static esp_err_t s_err;
    s_err = ESP_FAIL;
    esp_err_t err = svc_worker_submit(w_prewarm, &s_err);
    if (err == ESP_OK) {
        err = svc_worker_submit(w_done, done);
    }
    if (err == ESP_OK && xSemaphoreTake(done, pdMS_TO_TICKS(2000)) != pdTRUE) {
        err = ESP_ERR_TIMEOUT;
    }
    vSemaphoreDelete(done);
    return err == ESP_OK ? s_err : err;
}

esp_err_t svc_wifi_start(void)
{
    ESP_RETURN_ON_FALSE(!s.started, ESP_ERR_INVALID_STATE, TAG, "started");
    s.lock = s3w_mutex_create();
    ESP_RETURN_ON_FALSE(s.lock, ESP_ERR_NO_MEM, TAG, "lock");
    load();
    saved_to_status();
    // Normally made by svc_wifi_prewarm() already (lwIP's task, ~5 KB internal for good).
    ESP_RETURN_ON_ERROR(netif_up(), TAG, "netif");
    const esp_timer_create_args_t ta = {.callback = timer_cb, .name = "wifi"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&ta, &s.timer), TAG, "timer");
    // Wi-Fi left on unused drains the battery: off after WIFI_IDLE_OFF minutes (one-shot, re-armed).
    const esp_timer_create_args_t ia = {.callback = idle_cb, .name = "wifi_idle"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&ia, &s.idle_timer), TAG, "idle timer");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_idf_event, NULL, NULL),
                        TAG, "wifi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, on_idf_event, NULL, NULL), TAG,
                        "ip events");
    ESP_RETURN_ON_ERROR(s3w_event_subscribe(SVC_SETTINGS_EVENT, ESP_EVENT_ANY_ID, on_setting, NULL, NULL), TAG,
                        "settings");
    ESP_RETURN_ON_ERROR(wifi_link_start(), TAG, "link");
    s.started = true;
    ESP_LOGI(TAG, "%u saved network%s, switch %s", s.nets.count, s.nets.count == 1 ? "" : "s",
             svc_settings_get_bool(S3W_SETTING_WIFI) ? "on" : "off");
    return svc_worker_submit(w_apply, NULL);
}
