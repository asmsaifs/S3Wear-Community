// Connections page (connect_apps.h, docs/04 §4e). Rows, top to bottom:
//
//   Bluetooth     switch (setting BLUETOOTH)
//   Phone         Connected / Reconnecting… / Not paired / Bluetooth off
//   Last sync     "5 min ago" (paired only)
//   Reconnect     paired, Bluetooth on, not connected
//   Wi-Fi         Off / network name / Joining… / Not connected: opens the Wi-Fi page
//   Forget phone  red, confirmed (paired only)
//
// Wi-Fi page (P9-01):
//
//   Wi-Fi           switch (setting WIFI)
//   Network         name and signal / Joining… / Not connected (+ why) — when on
//   SAVED NETWORKS  one row each (tap: forget, confirmed), or "None yet"; added from the phone
//
// The pages are rebuilt from the backend's state when it changes (connect_apps_changed()) and
// when they become visible again; "Last sync" also counts up while it is shown.
// The Community edition has no Wi-Fi (docs/10 §3): no Wi-Fi row, no Wi-Fi page.
#include "connect_apps.h"

#include <stdio.h>
#include <string.h>

#include "s3w_edition.h"
#include "shell_priv.h"
#include "ui_overlay.h"
#include "ui_theme.h"
#include "ui_widgets.h"

// "Last sync" counts in minutes at best: refresh its text once a minute, and only while the
// page is visible (screen timers pause when it is covered or the display is off).
#define SYNC_TICK_MS 60000

// --- Backend --------------------------------------------------------------------------------

static void stub_read(connect_state_t *out, void *ctx)
{
    (void)ctx;
    *out = (connect_state_t){.sync_age_s = -1};
}

static esp_err_t stub_set_bluetooth(bool on, void *ctx)
{
    (void)on;
    (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t stub_action(void *ctx)
{
    (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t stub_wifi_forget(const char *ssid, void *ctx)
{
    (void)ssid;
    (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
}

static connect_backend_t s_be = {
    .read = stub_read,
    .set_bluetooth = stub_set_bluetooth,
    .reconnect = stub_action,
    .forget = stub_action,
    .set_wifi = stub_set_bluetooth,
    .wifi_forget = stub_wifi_forget,
};

void connect_apps_set_backend(const connect_backend_t *b)
{
    s_be.read = b && b->read ? b->read : stub_read;
    s_be.set_bluetooth = b && b->set_bluetooth ? b->set_bluetooth : stub_set_bluetooth;
    s_be.reconnect = b && b->reconnect ? b->reconnect : stub_action;
    s_be.forget = b && b->forget ? b->forget : stub_action;
    s_be.set_wifi = b && b->set_wifi ? b->set_wifi : stub_set_bluetooth;
    s_be.wifi_forget = b && b->wifi_forget ? b->wifi_forget : stub_wifi_forget;
    s_be.ctx = b ? b->ctx : NULL;
}

// --- Page -----------------------------------------------------------------------------------

typedef struct {
    ui_screen_t *screen;
    lv_obj_t *list;
    lv_obj_t *sync_value; // trailing label of "Last sync" (NULL when hidden)
    bool shown;           // on_resume has run once: later ones refresh
    char forget_ssid[CONNECT_SSID_MAX + 1]; // Wi-Fi page: the network the open dialog is about
} page_t;

#if S3W_EDITION_PRO
static const screen_def_t wifi_screen;
#endif

static void age_text(int32_t age_s, char *buf, size_t len)
{
    if (age_s < 0) {
        snprintf(buf, len, "Not yet");
    } else if (age_s < 60) {
        snprintf(buf, len, "Just now");
    } else if (age_s < 3600) {
        snprintf(buf, len, "%ld min ago", (long)(age_s / 60));
    } else if (age_s < 86400) {
        snprintf(buf, len, "%ld h ago", (long)(age_s / 3600));
    } else {
        const long d = (long)(age_s / 86400);
        snprintf(buf, len, "%ld day%s ago", d, d == 1 ? "" : "s");
    }
}

static const char *phone_text(const connect_state_t *st)
{
    if (!st->bluetooth) {
        return "Bluetooth off";
    }
    if (!st->paired) {
        return "Not paired";
    }
    return st->connected ? "Connected" : "Reconnecting…";
}

static void refresh_async(void *arg)
{
    (void)arg;
    connect_apps_changed();
}

static void bluetooth_changed(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target_obj(e);
    const bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    const esp_err_t err = s_be.set_bluetooth(on, s_be.ctx);
    if (err != ESP_OK) {
        ui_toast_show(err == ESP_ERR_NOT_SUPPORTED ? "Not available" : "Could not save", 0);
    }
    // Not from inside the switch's own event: the rebuild deletes it.
    lv_async_call(refresh_async, NULL);
}

static void reconnect_clicked(lv_event_t *e)
{
    (void)e;
    const esp_err_t err = s_be.reconnect(s_be.ctx);
    ui_toast_show(err == ESP_OK ? "Looking for your phone" : "Not available", 0);
}

#if S3W_EDITION_PRO
static void wifi_clicked(lv_event_t *e)
{
    (void)e;
    ui_nav_push(&wifi_screen, NULL);
}

// The Wi-Fi row's value on the Connections page.
static const char *wifi_summary(const connect_wifi_t *w)
{
    if (!w->on) {
        return "Off";
    }
    switch (w->state) {
    case CONNECT_WIFI_JOINED: return w->ssid;
    case CONNECT_WIFI_JOINING: return "Joining…";
    default: return "Not connected";
    }
}
#endif

static void forget_confirmed(bool ok, void *ctx)
{
    (void)ctx;
    if (!ok) {
        return;
    }
    const esp_err_t err = s_be.forget(s_be.ctx);
    ui_toast_show(err == ESP_OK                  ? "Phone forgotten"
                  : err == ESP_ERR_NOT_SUPPORTED ? "Not available yet"
                                                 : "Failed",
                  0);
    connect_apps_changed();
}

static void forget_clicked(lv_event_t *e)
{
    page_t *p = lv_event_get_user_data(e);
    s3w_dialog_show(ui_screen_root(p->screen), "Forget phone?", "The watch will need to be paired again.",
                    "Forget phone", true, forget_confirmed, NULL);
}

static void bottom_pad(lv_obj_t *list)
{
    lv_obj_t *pad = lv_obj_create(list);
    lv_obj_remove_style_all(pad);
    lv_obj_set_size(pad, 1, UI_SPACE_XL);
    lv_obj_remove_flag(pad, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

static lv_obj_t *info_row(lv_obj_t *list, const char *title, const char *subtitle, const char *value)
{
    lv_obj_t *row = s3w_list_add_row(list, NULL, title, subtitle, value);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    return row;
}

static void page_build(page_t *p)
{
    connect_state_t st;
    s_be.read(&st, s_be.ctx);
    char buf[32];

    lv_obj_update_layout(p->list);
    const int32_t y = lv_obj_get_scroll_y(p->list);
    lv_obj_clean(p->list);
    p->sync_value = NULL;
    s3w_header_create(p->list, "Connections");

    lv_obj_t *sw = s3w_toggle_row(p->list, NULL, "Bluetooth", st.bluetooth);
    lv_obj_add_event_cb(sw, bluetooth_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *phone = info_row(p->list, "Phone", st.bluetooth && !st.paired ? "Pair from phone" : NULL,
                               phone_text(&st));
    if (st.bluetooth && st.paired && st.connected) {
        lv_obj_set_style_text_color(lv_obj_get_child(phone, -1), ui_color(UI_COLOR_SUCCESS), 0);
    }
    if (st.paired) {
        age_text(st.sync_age_s, buf, sizeof buf);
        p->sync_value = lv_obj_get_child(info_row(p->list, "Last sync", NULL, buf), -1);
    }
    if (st.bluetooth && st.paired && !st.connected) {
        lv_obj_t *row = s3w_list_add_row(p->list, NULL, "Reconnect", NULL, NULL);
        lv_obj_add_event_cb(row, reconnect_clicked, LV_EVENT_CLICKED, p);
    }
#if S3W_EDITION_PRO
    lv_obj_t *wifi = s3w_list_add_row(p->list, NULL, "Wi-Fi", NULL, wifi_summary(&st.wifi));
    lv_obj_add_event_cb(wifi, wifi_clicked, LV_EVENT_CLICKED, p);
#endif
    if (st.paired) {
        lv_obj_t *row = s3w_list_add_row(p->list, NULL, "Forget phone", NULL, NULL);
        lv_obj_set_style_text_color(lv_obj_get_child(lv_obj_get_child(row, 0), 0), ui_color(UI_COLOR_DANGER), 0);
        lv_obj_add_event_cb(row, forget_clicked, LV_EVENT_CLICKED, p);
    }
    bottom_pad(p->list);
    lv_obj_update_layout(p->list);
    lv_obj_scroll_to_y(p->list, y, LV_ANIM_OFF);
}

static void sync_tick(lv_timer_t *t)
{
    page_t *p = lv_timer_get_user_data(t);
    if (p->sync_value) {
        connect_state_t st;
        s_be.read(&st, s_be.ctx);
        char buf[32];
        age_text(st.sync_age_s, buf, sizeof buf);
        lv_label_set_text(p->sync_value, buf);
    }
}

static void page_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)args;
    page_t *p = ui_screen_state(s);
    p->screen = s;
    p->list = s3w_list_create(root);
    page_build(p);
    ui_screen_timer_create(s, sync_tick, SYNC_TICK_MS, p);
}

static void page_resume(ui_screen_t *s)
{
    page_t *p = ui_screen_state(s);
    if (p->shown) {
        page_build(p); // the state may have changed while it was covered
    }
    p->shown = true;
}

static const screen_def_t connections_screen = {
    .id = "connections",
    .on_create = page_create,
    .on_resume = page_resume,
    .state_size = sizeof(page_t),
};

// --- Wi-Fi page -----------------------------------------------------------------------------
#if S3W_EDITION_PRO

static const char *signal_text(int8_t rssi)
{
    return rssi >= -60 ? "Strong signal" : rssi >= -72 ? "Good signal" : "Weak signal";
}

// Why the watch is not joined (subtitle of the Network row); NULL: nothing to say.
static const char *wifi_why(const connect_wifi_t *w, char *buf, size_t len)
{
    if (w->saved_count == 0) {
        return "No saved networks";
    }
    switch (w->error) {
    case CONNECT_WIFI_ERR_NOT_FOUND: return "No saved network nearby";
    case CONNECT_WIFI_ERR_AUTH: snprintf(buf, len, "Wrong password: %s", w->error_ssid); return buf;
    case CONNECT_WIFI_ERR_NO_IP: snprintf(buf, len, "No address from %s", w->error_ssid); return buf;
    case CONNECT_WIFI_ERR_OTHER:
        if (!w->error_ssid[0]) {
            return "Could not turn on";
        }
        snprintf(buf, len, "Could not join %s", w->error_ssid);
        return buf;
    default: return "Searching…";
    }
}

static void wifi_switch_changed(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target_obj(e);
    const esp_err_t err = s_be.set_wifi(lv_obj_has_state(sw, LV_STATE_CHECKED), s_be.ctx);
    if (err != ESP_OK) {
        ui_toast_show(err == ESP_ERR_NOT_SUPPORTED ? "Not available" : "Could not save", 0);
    }
    lv_async_call(refresh_async, NULL); // not from inside the switch's own event: the rebuild deletes it
}

static void wifi_forget_confirmed(bool ok, void *ctx)
{
    page_t *p = ctx;
    if (!ok) {
        return;
    }
    const esp_err_t err = s_be.wifi_forget(p->forget_ssid, s_be.ctx);
    ui_toast_show(err == ESP_OK ? "Network forgotten" : err == ESP_ERR_NOT_SUPPORTED ? "Not available" : "Failed", 0);
    connect_apps_changed();
}

static void wifi_saved_clicked(lv_event_t *e)
{
    page_t *p = lv_event_get_user_data(e);
    lv_obj_t *row = lv_event_get_current_target_obj(e);
    const uint32_t i = (uint32_t)(uintptr_t)lv_obj_get_user_data(row);
    connect_state_t st;
    s_be.read(&st, s_be.ctx);
    if (i >= st.wifi.saved_count) {
        return;
    }
    snprintf(p->forget_ssid, sizeof p->forget_ssid, "%s", st.wifi.saved[i]);
    static char title[CONNECT_SSID_MAX + 16]; // the dialog keeps the pointer while it is up
    snprintf(title, sizeof title, "Forget %s?", p->forget_ssid);
    s3w_dialog_show(ui_screen_root(p->screen), title, "The watch will no longer join it.", "Forget", true,
                    wifi_forget_confirmed, p);
}

static void wifi_build(page_t *p)
{
    connect_state_t st;
    s_be.read(&st, s_be.ctx);
    const connect_wifi_t *w = &st.wifi;
    char buf[64];

    lv_obj_update_layout(p->list);
    const int32_t y = lv_obj_get_scroll_y(p->list);
    lv_obj_clean(p->list);
    s3w_header_create(p->list, "Wi-Fi");

    lv_obj_t *sw = s3w_toggle_row(p->list, NULL, "Wi-Fi", w->on);
    lv_obj_add_event_cb(sw, wifi_switch_changed, LV_EVENT_VALUE_CHANGED, NULL);

    if (w->on) {
        lv_obj_t *net;
        if (w->state == CONNECT_WIFI_JOINED) {
            net = info_row(p->list, w->ssid, signal_text(w->rssi), "Connected");
            lv_obj_set_style_text_color(lv_obj_get_child(net, -1), ui_color(UI_COLOR_SUCCESS), 0);
        } else if (w->state == CONNECT_WIFI_JOINING) {
            info_row(p->list, w->ssid, NULL, "Joining…");
        } else {
            info_row(p->list, "Not connected", wifi_why(w, buf, sizeof buf), NULL);
        }
    }

    s3w_list_add_section(p->list, "Saved networks");
    for (uint32_t i = 0; i < w->saved_count && i < CONNECT_WIFI_SAVED_MAX; i++) {
        const bool cur = w->state == CONNECT_WIFI_JOINED && strcmp(w->saved[i], w->ssid) == 0;
        lv_obj_t *row = s3w_list_add_row(p->list, NULL, w->saved[i], NULL, cur ? "Connected" : NULL);
        lv_obj_set_user_data(row, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(row, wifi_saved_clicked, LV_EVENT_CLICKED, p);
    }
    info_row(p->list, w->saved_count ? "Add more" : "None yet", "From the phone app", NULL);
    bottom_pad(p->list);
    lv_obj_update_layout(p->list);
    lv_obj_scroll_to_y(p->list, y, LV_ANIM_OFF);
}

static void wifi_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)args;
    page_t *p = ui_screen_state(s);
    p->screen = s;
    p->list = s3w_list_create(root);
    wifi_build(p);
}

static void wifi_resume(ui_screen_t *s)
{
    page_t *p = ui_screen_state(s);
    if (p->shown) {
        wifi_build(p);
    }
    p->shown = true;
}

static const screen_def_t wifi_screen = {
    .id = "wifi",
    .on_create = wifi_create,
    .on_resume = wifi_resume,
    .state_size = sizeof(page_t),
};
#endif

void connect_apps_changed(void)
{
    ui_screen_t *top = ui_nav_top();
    if (!top || !ui_screen_is_visible(top)) {
        return;
    }
    if (ui_screen_def(top) == &connections_screen) {
        page_build(ui_screen_state(top));
    }
#if S3W_EDITION_PRO
    else if (ui_screen_def(top) == &wifi_screen) {
        wifi_build(ui_screen_state(top));
    }
#endif
}

void connect_apps_init(void)
{
    static bool s_done;
    if (s_done) {
        return;
    }
    s_done = true;
    ui_nav_register(&connections_screen);
#if S3W_EDITION_PRO
    ui_nav_register(&wifi_screen);
#endif
}
