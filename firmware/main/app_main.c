// S3Wear firmware entry point. Boot sequence only (docs/02-firmware-architecture.md §2).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "battery_apps.h"
#include "bsp_s3w.h"
#include "clock_apps.h"
#include "bsp_s3w_pins.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "hal.h"
#include "settings_apps.h"
#include "shell.h"
#include "svc_alarm.h"
#include "svc_audio.h"
#include "s3w_lvgl_port.h"
#include "svc_diag.h"
#include "svc_input.h"
#include "svc_modes.h"
#include "svc_power.h"
#include "svc_sensors.h"
#include "svc_settings.h"
#include "svc_storage.h"
#include "svc_time.h"
#include "svc_worker.h"
#include "sys_core.h"
#include "ui_nav.h"
#include "ui_overlay.h"
#include "ui_root.h"
#include "ui_screens.h"
#include "ui_theme.h"
#include "watch_only.h"
#include "wf_engine.h"
#include "wf_shift.h"

static const char *TAG = "app_main";

#define BOOT_BRIGHTNESS     200

// Largest single transfer: one internal DMA buffer (ui_framework Kconfig, docs/02 §6).
#define LCD_MAX_TRANSFER S3W_LVGL_PORT_BUF_BYTES(BSP_LCD_H_RES, CONFIG_S3W_LVGL_BUF_LINES)

// Panel + LVGL port. keep_frame: attach to the panel left on in WATCH-ONLY.
static esp_err_t lvgl_start(bool keep_frame)
{
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io;
    ESP_RETURN_ON_ERROR(bsp_display_new(LCD_MAX_TRANSFER, NULL, keep_frame, &panel, &io), TAG, "panel");
    const s3w_lvgl_port_cfg_t cfg = {
        .panel = panel,
        .io = io,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .buf_lines = CONFIG_S3W_LVGL_BUF_LINES,
        .te_wait = bsp_display_te_wait,
    };
    return s3w_lvgl_port_init(&cfg);
}

static esp_err_t display_start(void)
{
    ESP_RETURN_ON_ERROR(lvgl_start(false), TAG, "display");
    lv_lock();
    ui_theme_init(s3w_lvgl_port_display());
    ui_boot_screen_show();
    lv_refr_now(NULL); // first frame before the panel lights up
    lv_unlock();
    return hal_display_set_brightness(BOOT_BRIGHTNESS);
}

// --- WATCH-ONLY (docs/02 §7) ------------------------------------------------------------

// What the minute tick boots need to draw the time without NVS or svc_time: written
// before every WATCH-ONLY deep sleep; survives it (the magic tells a power loss).
#define WO_FACE_MAGIC 0x57A7F00Du
typedef struct {
    uint32_t magic;
    bool h24;
    bool valid;
    char tz[64]; // POSIX TZ as svc_time set it
} wo_face_t;
static RTC_NOINIT_ATTR wo_face_t s_wo_face;

// The time-only screen for the minute a tick at now shows, with its burn-in step.
static void watch_only_draw(int8_t battery_pct)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    const int64_t minute = watch_only_minute((int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000);
    ui_watch_only_args_t args = {.at = (time_t)(minute * 60), .battery_pct = battery_pct};
    wf_aod_shift((uint32_t)minute, &args.dx, &args.dy);
    ui_watch_only_show(&args);
}

// Minute tick boot: redraw the time on the panel that stayed on, then deep sleep
// again. Only the PMU (USB check, battery %), the panel and LVGL: no NVS, no services.
// Returns only when USB power is back (the boot continues normally).
static void watch_only_tick(void)
{
    hal_battery_t bat = {.percent = -1};
    hal_pmu_read_battery(&bat);
    if (bat.vbus) {
        ESP_LOGI(TAG, "watch-only: USB power, leaving it");
        svc_power_watch_only_exit();
        return;
    }
    const bool dark = bat.percent >= 0 && bat.percent <= WATCH_ONLY_DARK_PCT;
    if (dark) {
        esp_lcd_panel_handle_t panel;
        esp_lcd_panel_io_handle_t io;
        bsp_display_new(LCD_MAX_TRANSFER, NULL, true, &panel, &io); // to switch it off
    } else if (sys_core_init() == ESP_OK && lvgl_start(true) == ESP_OK) {
        const bool known = s_wo_face.magic == WO_FACE_MAGIC;
        if (known && s_wo_face.tz[0]) {
            setenv("TZ", s_wo_face.tz, 1);
            tzset();
        }
        lv_lock();
        ui_theme_init(s3w_lvgl_port_display());
        ui_clock_set_24h(!known || s_wo_face.h24);
        ui_clock_set_valid(known && s_wo_face.valid);
        watch_only_draw(bat.percent);
        lv_refr_now(NULL);
        lv_unlock();
        // A command waits for the queued pixel DMA: the frame is complete before sleep.
        esp_lcd_panel_disp_on_off(bsp_display_panel(), true);
    }
    svc_power_watch_only_sleep(dark);
}

// --- svc_power glue (all on the UI task) ---------------------------------------------

// Screen state from svc_power. It waits for this to return before it switches the panel.
static void power_ui_hook(svc_power_ui_mode_t mode)
{
    const bool on = mode == SVC_POWER_UI_ON;
    s3w_lvgl_port_set_low_power(!on);
    ui_pointer_set_enabled(on);
    if (mode == SVC_POWER_UI_WATCH_ONLY) {
        // Deep sleep follows; the minute ticks redraw this from s_wo_face.
        s_wo_face.h24 = ui_clock_is_24h();
        s_wo_face.valid = ui_clock_is_valid();
        const char *tz = getenv("TZ");
        snprintf(s_wo_face.tz, sizeof s_wo_face.tz, "%s", tz ? tz : "");
        s_wo_face.magic = WO_FACE_MAGIC;
        ui_nav_set_active(false);
        ui_overlay_clear();
        svc_power_battery_t bat = {.percent = -1};
        svc_power_battery(&bat);
        watch_only_draw(bat.percent);
        s3w_lvgl_port_set_output(true);
        lv_refr_now(NULL);
        return;
    }
    if (mode == SVC_POWER_UI_OFF) {
        ui_nav_set_active(false);
        s3w_lvgl_port_set_output(false);
        return;
    }
    // AOD: the face's AOD variant on the home screen (burn-in shift: P3-05).
    if (mode == SVC_POWER_UI_AOD) {
        ui_nav_home();
    }
    wf_set_aod(mode == SVC_POWER_UI_AOD);
    s3w_lvgl_port_set_output(true);
    ui_nav_set_active(true);
    lv_refr_now(NULL); // the panel lights up after this frame
}

static void on_power_action(ui_power_action_t action, void *ctx)
{
    (void)ctx;
    switch (action) {
    case UI_POWER_ACTION_OFF:
        svc_power_shutdown();
        break;
    case UI_POWER_ACTION_RESTART:
        svc_power_restart();
        break;
    case UI_POWER_ACTION_SAVER_ON:
    case UI_POWER_ACTION_SAVER_OFF:
        svc_power_set_saver(action == UI_POWER_ACTION_SAVER_ON);
        break;
    case UI_POWER_ACTION_WATCH_ONLY:
        svc_power_enter_watch_only();
        break;
    }
}

static void show_power_menu(void)
{
    const ui_screen_t *top = ui_nav_top();
    if (top && ui_screen_def(top) == &ui_power_menu_screen) {
        return;
    }
    svc_power_battery_t bat = {.percent = -1};
    svc_power_battery(&bat);
    const ui_power_menu_args_t args = {
        .on_action = on_power_action,
        .saver = svc_power_saver(),
        .charging = bat.charging,
        .battery_pct = bat.percent,
    };
    ui_nav_push(&ui_power_menu_screen, &args);
}

// svc_input actions (docs/03 F3): BOOT = back, or the launcher on the home screen;
// PWR = home, or screen off on the home screen, or snooze while an alarm rings;
// PWR held 2 s = power menu. SOS and the BOOT shortcut get their
// features later (P4, P3): a toast for now.
static void on_input_action(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    (void)id;
    if (len < sizeof(svc_input_evt_action_t)) {
        return;
    }
    switch ((svc_input_action_t)((const svc_input_evt_action_t *)data)->action) {
    case SVC_INPUT_ACTION_BACK:
        if (!ui_nav_back()) {
            ui_nav_home_swipe(LV_DIR_RIGHT); // BOOT on the face opens the launcher (docs/03 F3)
        }
        break;
    case SVC_INPUT_ACTION_HOME:
        if (clock_apps_ring_active()) {
            clock_apps_ring_key(); // PWR snoozes a ringing alarm, stops a timer
        } else if (ui_nav_depth() <= 1) {
            svc_power_screen_off();
        } else {
            ui_nav_home();
        }
        break;
    case SVC_INPUT_ACTION_POWER_MENU:
        show_power_menu();
        break;
    case SVC_INPUT_ACTION_SHORTCUT:
        ui_toast_show("Shortcut", 1500);
        break;
    case SVC_INPUT_ACTION_SOS:
        ui_toast_show("SOS", 2000);
        break;
    default:
        break;
    }
}

// svc_time: valid flag, 12/24 h, and a re-render after a set, step, zone or DST change.
static void on_time_changed(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    (void)id;
    if (len < sizeof(svc_time_evt_changed_t)) {
        return;
    }
    const svc_time_evt_changed_t *evt = data;
    ui_clock_set_valid(evt->valid);
    ui_clock_set_24h(evt->h24);
    ui_clock_refresh();
}

// Watch face data from services (more sources join with their services: steps P4,
// phone link P5, ...), and the battery screens.
static void on_battery(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    (void)id;
    if (len < sizeof(svc_power_battery_t)) {
        return;
    }
    const svc_power_battery_t *b = data;
    wf_data_t *d = wf_data_edit();
    d->battery_pct = b->percent;
    d->charging = b->charging;
    wf_data_changed(WF_DATA_BATTERY);

    static bool s_vbus;
    if (b->vbus != s_vbus) {
        s_vbus = b->vbus;
        battery_apps_charger(b->vbus); // plugged: charging screen; unplugged: close it
    }
    battery_apps_changed();
}

// Battery screens backend (battery_apps.h) over svc_power.
static void ba_read(battery_info_t *out, void *ctx)
{
    (void)ctx;
    static svc_power_battery_info_t s_info; // UI task only; too big for its stack
    if (svc_power_battery_info(&s_info) != ESP_OK) {
        s_info.battery = (svc_power_battery_t){.percent = -1};
    }
    *out = (battery_info_t){
        .percent = s_info.battery.percent,
        .mv = s_info.battery.mv,
        .charging = s_info.battery.charging,
        .vbus = s_info.battery.vbus,
        .saver = svc_power_saver(),
        .minutes = s_info.minutes,
    };
    memcpy(out->history, s_info.history, sizeof out->history);
}

static void ba_saver(bool on, void *ctx)
{
    (void)ctx;
    svc_power_set_saver(on);
}

static void ba_watch_only(void *ctx)
{
    (void)ctx;
    svc_power_enter_watch_only();
}

// 15 % toast, 10 % offer saver, 3 % watch-only alert (svc_power wakes the screen for
// the last two, except in DND, sleep and theater mode).
static void on_battery_low(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    (void)id;
    if (len >= sizeof(svc_power_evt_battery_low_t)) {
        const svc_power_evt_battery_low_t *e = data;
        battery_apps_low(e->threshold, e->percent);
    }
}

// Screen woke: on USB power with the face on top, the charging screen (docs/03 F5).
static void on_power_state(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    (void)id;
    if (len >= sizeof(svc_power_evt_state_t)) {
        const svc_power_evt_state_t *e = data;
        if (e->state == POWER_STATE_ACTIVE && !power_state_screen_on((power_state_t)e->prev)) {
            battery_apps_screen_on();
        }
        battery_apps_changed(); // saver flag
    }
}

// Installed declarative faces (/flash/faces/<id>/face.json, P3-03).
#define FACES_DIR SVC_STORAGE_FLASH_PATH "/faces"

static void on_face_loaded(const char *path, esp_err_t result, const wf_decl_err_t *err, void *ctx)
{
    (void)ctx;
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "face %s loaded", path);
    } else if (err->line > 0) {
        ESP_LOGW(TAG, "face %s:%d:%d: %s", path, err->line, err->col, err->msg);
    } else {
        ESP_LOGW(TAG, "face %s: %s", path, err->msg);
    }
}

// WATCH_FACE setting (face picker P3-04, console `face`): show that face.
static void apply_face_setting(void)
{
    char id[48];
    if (svc_settings_get_str(S3W_SETTING_WATCH_FACE, id, sizeof id) != ESP_OK || wf_set_active(id) != ESP_OK) {
        ESP_LOGW(TAG, "watch face '%s' not found, using %s", id, wf_active()->id);
    }
}

// FACE_CONFIG setting (slots and colours from the customize screen, wf_cfg.h).
// UI task only (boot with the LVGL lock held, then settings events and the face
// listener), so one static buffer serves load and save.
static char s_face_cfg[512];

static void apply_face_config(void)
{
    if (svc_settings_get_str(S3W_SETTING_FACE_CONFIG, s_face_cfg, sizeof s_face_cfg) != ESP_OK) {
        s_face_cfg[0] = '\0';
    }
    const int bad = wf_config_load(s_face_cfg);
    if (bad > 0) {
        ESP_LOGW(TAG, "face config: %d item(s) skipped (face removed?)", bad);
    }
}

// The picker or customize screen changed something: save it.
static void on_face_changed(wf_change_t what, void *ctx)
{
    (void)ctx;
    esp_err_t err;
    if (what == WF_CHANGE_ACTIVE) {
        err = svc_settings_set_str(S3W_SETTING_WATCH_FACE, wf_active()->id);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "save face: %s", esp_err_to_name(err));
        }
        return;
    }
    if (wf_config_save(s_face_cfg, sizeof s_face_cfg) != ESP_OK) {
        // Saves the items that fit; the rest is lost at reboot.
        ESP_LOGW(TAG, "face config longer than %u bytes", (unsigned)sizeof s_face_cfg - 1);
        ui_toast_show("Too many custom faces to save all", 0);
    }
    err = svc_settings_set_str(S3W_SETTING_FACE_CONFIG, s_face_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save face config: %s", esp_err_to_name(err));
    }
}

// WORLD_CLOCKS setting (city ids, world_clock.h) -> the world clock app and complication.
static void apply_world_clocks(void)
{
    char csv[96];
    if (svc_settings_get_str(S3W_SETTING_WORLD_CLOCKS, csv, sizeof csv) != ESP_OK) {
        csv[0] = '\0';
    }
    clock_apps_set_world(csv);
}

static void on_world_changed(const char *csv, void *ctx)
{
    (void)ctx;
    const esp_err_t err = svc_settings_set_str(S3W_SETTING_WORLD_CLOCKS, csv);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save world clocks: %s", esp_err_to_name(err));
    }
}

static void on_setting(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    const s3w_setting_t which =
        len >= sizeof(svc_settings_evt_changed_t) ? ((const svc_settings_evt_changed_t *)data)->id : S3W_SETTING_COUNT;
    if (id == SVC_SETTINGS_EVT_RESET || which == S3W_SETTING_FACE_CONFIG) {
        apply_face_config(); // no rebuild if the active face's configuration did not change
    }
    if (id == SVC_SETTINGS_EVT_RESET || which == S3W_SETTING_WATCH_FACE) {
        apply_face_setting();
    }
    if (id == SVC_SETTINGS_EVT_RESET || which == S3W_SETTING_LAUNCHER_GRID) {
        shell_launcher_set_grid(svc_settings_get_bool(S3W_SETTING_LAUNCHER_GRID));
    }
    if (id == SVC_SETTINGS_EVT_RESET || which == S3W_SETTING_WORLD_CLOCKS) {
        apply_world_clocks();
    }
}

// Quick settings backend (shell.h). Wi-Fi (P9) and Bluetooth (P4) join with their
// features; until then they say "not available yet".
static void qs_read(shell_qs_state_t *st, void *ctx)
{
    (void)ctx;
    modes_state_t modes;
    svc_modes_get(&modes);
    st->available = (1u << SHELL_QS_DND) | (1u << SHELL_QS_THEATER) | (1u << SHELL_QS_SLEEP) |
                    (1u << SHELL_QS_AOD) | (1u << SHELL_QS_SILENT) | (1u << SHELL_QS_SAVER);
    st->on = (modes.dnd ? 1u << SHELL_QS_DND : 0) | (modes.theater ? 1u << SHELL_QS_THEATER : 0) |
             (modes.sleep ? 1u << SHELL_QS_SLEEP : 0) |
             (svc_settings_get_bool(S3W_SETTING_AOD) ? 1u << SHELL_QS_AOD : 0) |
             (svc_settings_get_bool(S3W_SETTING_SILENT) ? 1u << SHELL_QS_SILENT : 0) |
             (svc_power_saver() ? 1u << SHELL_QS_SAVER : 0);
    st->brightness = (uint8_t)svc_settings_get_int(S3W_SETTING_DISPLAY_BRIGHTNESS);
    svc_power_battery_t bat = {.percent = -1};
    svc_power_battery(&bat);
    st->battery_pct = bat.percent;
    st->charging = bat.charging;
    st->phone_connected = false; // phone link: P4
}

static void qs_toggle(shell_qs_item_t item, bool on, void *ctx)
{
    (void)ctx;
    esp_err_t err = ESP_OK;
    switch (item) {
    case SHELL_QS_DND:
        err = svc_modes_set(MODE_DND, on);
        break;
    case SHELL_QS_THEATER:
        err = svc_modes_set(MODE_THEATER, on); // svc_power turns the screen off
        break;
    case SHELL_QS_SLEEP:
        err = svc_modes_set(MODE_SLEEP, on);
        break;
    case SHELL_QS_AOD:
        err = svc_settings_set_bool(S3W_SETTING_AOD, on); // svc_power follows the setting
        break;
    case SHELL_QS_SILENT:
        err = svc_settings_set_bool(S3W_SETTING_SILENT, on);
        break;
    case SHELL_QS_SAVER:
        err = svc_power_set_saver(on);
        break;
    default:
        break;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "quick settings %s: %s", shell_qs_name(item), esp_err_to_name(err));
    }
}

static void qs_brightness(uint8_t pct, void *ctx)
{
    (void)ctx;
    const esp_err_t err = svc_settings_set_int(S3W_SETTING_DISPLAY_BRIGHTNESS, pct); // svc_power applies it
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "brightness %u: %s", pct, esp_err_to_name(err));
    }
}

// Settings app backend (settings_apps.h) over svc_settings. Battery saver is svc_power's.
static int32_t sa_get_int(s3w_setting_t id, void *ctx)
{
    (void)ctx;
    if (id == S3W_SETTING_BATTERY_SAVER) {
        return svc_power_saver();
    }
    return settings_info(id)->type == S3W_SETTING_TYPE_BOOL ? svc_settings_get_bool(id) : svc_settings_get_int(id);
}

static esp_err_t sa_set_int(s3w_setting_t id, int32_t v, void *ctx)
{
    (void)ctx;
    if (id == S3W_SETTING_BATTERY_SAVER) {
        return svc_power_set_saver(v != 0);
    }
    return settings_info(id)->type == S3W_SETTING_TYPE_BOOL ? svc_settings_set_bool(id, v != 0)
                                                           : svc_settings_set_int(id, v);
}

static void sa_get_str(s3w_setting_t id, char *buf, size_t len, void *ctx)
{
    (void)ctx;
    if (svc_settings_get_str(id, buf, len) != ESP_OK && len > 0) {
        buf[0] = '\0';
    }
}

static esp_err_t sa_set_str(s3w_setting_t id, const char *v, void *ctx)
{
    (void)ctx;
    return svc_settings_set_str(id, v);
}

static esp_err_t sa_action(settings_action_t action, void *ctx)
{
    (void)ctx;
    switch (action) {
    case SETTINGS_ACT_RESTART:
        return svc_power_restart();
    case SETTINGS_ACT_POWER_OFF:
        return svc_power_shutdown();
    case SETTINGS_ACT_FACTORY_RESET: {
        // Settings only for now; bonds and the storage partition join with F19.
        const esp_err_t err = svc_settings_factory_reset();
        return err == ESP_OK ? svc_power_restart() : err;
    }
    case SETTINGS_ACT_FORGET_PHONE:
        return ESP_ERR_NOT_SUPPORTED; // the phone link is P4
    }
    return ESP_ERR_INVALID_ARG;
}

static void sa_about(settings_about_t *out, void *ctx)
{
    (void)ctx;
    const esp_app_desc_t *desc = esp_app_get_description();
    snprintf(out->version, sizeof out->version, "%s", desc->version);
    snprintf(out->idf, sizeof out->idf, "%s", desc->idf_ver);
    // A LittleFS usage read on the UI task: it walks the metadata, a few ms, only when About is built.
    uint64_t total = 0;
    uint64_t used = 0;
    if (svc_storage_usage(SVC_STORAGE_FLASH_PATH, &total, &used) == ESP_OK && total > 0) {
        snprintf(out->storage, sizeof out->storage, "%.1f of %.0f MB free", (double)(total - used) / (1024 * 1024),
                 (double)total / (1024 * 1024));
    }
}

// Launcher layout switched in the launcher: save it.
static void on_launcher_layout(bool grid, void *ctx)
{
    (void)ctx;
    const esp_err_t err = svc_settings_set_bool(S3W_SETTING_LAUNCHER_GRID, grid);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save launcher layout: %s", esp_err_to_name(err));
    }
}

// Clock apps backend (clock_apps.h) over svc_alarm. Every call only takes svc_alarm's
// mutex and queues the slow part, so it is fine on the UI task.
_Static_assert((int)CLOCK_TIMER_PAUSE == (int)SVC_TIMER_PAUSE && (int)CLOCK_TIMER_RESUME == (int)SVC_TIMER_RESUME &&
                   (int)CLOCK_TIMER_RESTART == (int)SVC_TIMER_RESTART &&
                   (int)CLOCK_TIMER_REMOVE == (int)SVC_TIMER_REMOVE,
               "clock_apps timer actions map 1:1 onto svc_alarm's");

static void ca_alarms(alarm_set_t *out, void *ctx)
{
    (void)ctx;
    svc_alarm_get(out);
}

static esp_err_t ca_alarm_put(const alarm_t *a, void *ctx)
{
    (void)ctx;
    return svc_alarm_put(a, NULL);
}

static void ca_alarm_delete(uint8_t id, void *ctx)
{
    (void)ctx;
    svc_alarm_delete(id);
}

static int64_t ca_alarm_next(void *ctx)
{
    (void)ctx;
    return svc_alarm_next();
}

static void ca_timers(timer_set_t *out, uint32_t *now_ms, void *ctx)
{
    (void)ctx;
    svc_alarm_timers(out, now_ms);
}

static esp_err_t ca_timer_start(uint32_t ms, void *ctx)
{
    (void)ctx;
    return svc_alarm_timer_start(ms, NULL);
}

static void ca_timer_action(uint8_t id, clock_timer_action_t action, void *ctx)
{
    (void)ctx;
    svc_alarm_timer_action(id, (svc_timer_action_t)action);
}

static void ca_snooze(void *ctx)
{
    (void)ctx;
    svc_alarm_snooze();
}

static void ca_dismiss(void *ctx)
{
    (void)ctx;
    svc_alarm_dismiss();
}

static void show_ring(const svc_alarm_evt_ring_t *r)
{
    clock_ring_t c = {
        .kind = r->kind == SVC_ALARM_RING_TIMER ? CLOCK_RING_TIMER : CLOCK_RING_ALARM,
        .id = r->id,
        .snooze_min = r->snooze_min,
        .at = r->at,
        .duration_ms = r->duration_ms,
    };
    memcpy(c.label, r->label, sizeof c.label);
    clock_apps_ring(&c);
}

static void on_alarm(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    switch ((svc_alarm_event_t)id) {
    case SVC_ALARM_EVT_CHANGED:
        clock_apps_changed();
        break;
    case SVC_ALARM_EVT_RING:
        if (len >= sizeof(svc_alarm_evt_ring_t)) {
            show_ring(data);
        }
        break;
    case SVC_ALARM_EVT_RING_END:
        if (len >= sizeof(svc_alarm_evt_ring_end_t)) {
            const svc_alarm_evt_ring_end_t *e = data;
            clock_apps_ring_end(e->snooze_min > 0, e->snooze_min); // snooze_min is set only when snoozed
        }
        break;
    }
}

// UI_SCREEN_KEEP_ON: hold the screen on while such a screen is visible.
static void on_nav_changed(void *ctx)
{
    (void)ctx;
    static bool s_held;
    const ui_screen_t *top = ui_nav_top();
    const bool keep = top && ui_screen_is_visible(top) && (ui_screen_def(top)->flags & UI_SCREEN_KEEP_ON);
    if (keep != s_held) {
        s_held = keep;
        svc_power_hold_screen(keep);
    }
}

// Peripheral bring-up failures are logged, not fatal, so the console and factory
// test can still report which chip is missing.
static void boot_step(const char *what, esp_err_t err)
{
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s failed: %s", what, esp_err_to_name(err));
    }
}

void app_main(void)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    ESP_LOGI(TAG, "S3Wear boot (%s, IDF %s)", desc->version, desc->idf_ver);

    // 1. Board: PA low, I2C bus, buttons; PMU (charger limits, power key, IRQ polling).
    ESP_ERROR_CHECK(bsp_init_early());
    // Default loop: IDF (Wi-Fi/netif) and BSP driver events. Services use the
    // sys_core bus (sys_core_init below).
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    boot_step("PMU", bsp_pmu_start());

    // WATCH-ONLY minute tick: redraw the time and deep sleep again (no return unless
    // USB power came back).
    if (svc_power_boot_kind() == SVC_POWER_BOOT_WATCH_TICK) {
        watch_only_tick();
    }

    // 3. sys_core (logging policy, UI mailbox, event bus), worker; NVS + settings
    // (settings fall back to defaults in RAM if NVS fails).
    ESP_ERROR_CHECK(sys_core_init());
    ESP_ERROR_CHECK(svc_worker_start());
    boot_step("settings", svc_settings_init());
    boot_step("metrics", svc_diag_metrics_start()); // needs NVS and the worker

    // 4. Time: RTC -> system time ("time unknown" if it lost power), zone, drift trim.
    boot_step("RTC", bsp_rtc_start());
    boot_step("time", svc_time_start());

    // 5. Display + touch + LVGL, boot logo.
    boot_step("display", display_start());
    boot_step("touch", bsp_touch_start());
    if (bsp_touch_handle() && s3w_lvgl_port_display()) {
        lv_lock();
        ui_pointer_create(s3w_lvgl_port_display());
        lv_unlock();
    }

    // IMU (sensors off until a service configures them).
    boot_step("IMU", bsp_imu_start());

    // Audio codecs (powered down until used).
    boot_step("audio", bsp_audio_start());

    // 6. Storage: LittleFS now, SD card in the background (optional).
    boot_step("flash fs", svc_storage_mount_flash());
    boot_step("sd", svc_storage_start());

    // 7. Services. svc_modes: DND / sleep / theater (svc_power and svc_sensors read
    // it at start). svc_power: screen states, light sleep, battery. svc_input:
    // buttons and palm cover; svc_sensors: raise to wake (both need svc_power).
    boot_step("modes", svc_modes_start());
    svc_power_set_ui_hook(s3w_lvgl_port_display() ? power_ui_hook : NULL);
    boot_step("power", svc_power_start());
    boot_step("input", svc_input_start());
    boot_step("sensors", svc_sensors_start());
    // svc_audio: system sounds (after settings, modes and power events); svc_alarm rings through it.
    boot_step("sounds", svc_audio_start());
    // svc_alarm: alarms, timers, ringing (after time, power and sensors).
    boot_step("alarm", svc_alarm_start());

    // Diagnostics console (USB-Serial-JTAG).
    ESP_ERROR_CHECK(svc_diag_console_start());

    // 9. Home screen: the watch face, and the shell around it.
    if (s3w_lvgl_port_display()) {
        lv_lock();
        ui_clock_set_24h(svc_time_is_24h());
        ui_clock_set_valid(svc_time_is_valid());
        wf_init();
        // Reads a few small files while holding the LVGL lock; boot only, before ui_start().
        wf_decl_load_dir(FACES_DIR, on_face_loaded, NULL);
        apply_face_config(); // after every face is registered
        apply_face_setting();
        wf_set_listener(on_face_changed, NULL);
        // Quick settings, notifications, tiles, launcher around the face (P3-06).
        shell_init();
        const shell_qs_backend_t qs = {.read = qs_read, .toggle = qs_toggle, .brightness = qs_brightness};
        shell_qs_set_backend(&qs);
        shell_launcher_set_grid(svc_settings_get_bool(S3W_SETTING_LAUNCHER_GRID));
        shell_launcher_set_listener(on_launcher_layout, NULL);
        // Settings app (P3-10).
        const settings_app_backend_t settings = {
            .get_int = sa_get_int,
            .set_int = sa_set_int,
            .get_str = sa_get_str,
            .set_str = sa_set_str,
            .action = sa_action,
            .about = sa_about,
        };
        settings_apps_set_backend(&settings);
        // Alarms, timers, stopwatch, world clock (P3-07).
        const clock_backend_t clock = {
            .alarms = ca_alarms,
            .alarm_put = ca_alarm_put,
            .alarm_delete = ca_alarm_delete,
            .alarm_next = ca_alarm_next,
            .timers = ca_timers,
            .timer_start = ca_timer_start,
            .timer_action = ca_timer_action,
            .ring_snooze = ca_snooze,
            .ring_dismiss = ca_dismiss,
        };
        clock_apps_set_backend(&clock);
        apply_world_clocks();
        clock_apps_set_world_listener(on_world_changed, NULL);
        // Battery app, charging screen, low-battery flows (P3-09).
        const battery_backend_t battery = {.read = ba_read, .set_saver = ba_saver, .watch_only = ba_watch_only};
        battery_apps_set_backend(&battery);
        svc_power_battery_t bat;
        if (svc_power_battery(&bat) == ESP_OK) {
            on_battery(NULL, NULL, 0, &bat, sizeof bat);
        }
        boot_step("ui", ui_start());
        ui_nav_set_listener(on_nav_changed, NULL);
        lv_unlock();
        s3w_ui_subscribe(SVC_INPUT_EVENT, SVC_INPUT_EVT_ACTION, on_input_action, NULL, NULL);
        s3w_ui_subscribe(SVC_TIME_EVENT, SVC_TIME_EVT_CHANGED, on_time_changed, NULL, NULL);
        s3w_ui_subscribe(SVC_POWER_EVENT, SVC_POWER_EVT_BATTERY, on_battery, NULL, NULL);
        s3w_ui_subscribe(SVC_POWER_EVENT, SVC_POWER_EVT_BATTERY_LOW, on_battery_low, NULL, NULL);
        s3w_ui_subscribe(SVC_POWER_EVENT, SVC_POWER_EVT_STATE, on_power_state, NULL, NULL);
        s3w_ui_subscribe(SVC_SETTINGS_EVENT, ESP_EVENT_ANY_ID, on_setting, NULL, NULL);
        s3w_ui_subscribe(SVC_ALARM_EVENT, ESP_EVENT_ANY_ID, on_alarm, NULL, NULL);
        // An alarm that started ringing during boot (deep-sleep wake) before the
        // subscription: show it now (a second RING event for it is ignored).
        svc_alarm_evt_ring_t ring;
        if (svc_alarm_ringing(&ring)) {
            lv_lock();
            show_ring(&ring);
            lv_unlock();
        }
    }
}
