// Factory test runner. Tests run in their own task (never the UI task); the checklist
// screen is updated under lv_lock(). Result JSON goes to stdout as a single line.
#include "factory_test.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bsp_s3w.h"
#include "drv_audio.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ft_radio.h"
#include "ft_result.h"
#include "s3w_lvgl_port.h"
#include "svc_sensors.h"
#include "svc_storage.h"

static const char *TAG = "factory_test";

#define FT_TASK_STACK       8192
#define FT_TASK_PRIO        5
#define I2C_PROBE_MS        20
#define TE_WINDOW_MS        200
#define TE_MIN_EDGES        8  // 60 Hz panel: ~12 edges in 200 ms
#define INTERACT_TIMEOUT_MS 30000
#define BLE_ADV_MS          2000
#define LOOPBACK_HZ         1000
#define JSON_MAX            1536

typedef ft_status_t (*ft_fn_t)(char *detail, size_t len);

typedef struct {
    const char *name;
    ft_fn_t fn;
    bool interactive;
} ft_item_t;

// --- UI -------------------------------------------------------------------------

static struct {
    lv_obj_t *prompt;
    lv_obj_t *status[16];
    lv_obj_t *summary;
} s_ui;

static lv_color_t status_color(ft_status_t s)
{
    switch (s) {
    case FT_PASS:
        return lv_color_hex(0x30D158);
    case FT_FAIL:
        return lv_color_hex(0xFF453A);
    case FT_SKIP:
        return lv_color_hex(0x8E8E93);
    case FT_RUNNING:
        return lv_color_hex(0xFFD60A);
    default:
        return lv_color_hex(0x636366);
    }
}

static bool ui_ready(void)
{
    return s3w_lvgl_port_display() != NULL;
}

static void ui_build(const ft_item_t *items, size_t n)
{
    if (!ui_ready()) {
        return;
    }
    lv_lock();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_pad_all(scr, 16, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr, 2, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Factory test");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);

    s_ui.prompt = lv_label_create(scr);
    lv_label_set_text(s_ui.prompt, "");
    lv_obj_set_style_text_font(s_ui.prompt, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.prompt, lv_color_hex(0xFFD60A), 0);

    for (size_t i = 0; i < n && i < sizeof s_ui.status / sizeof s_ui.status[0]; i++) {
        lv_obj_t *row = lv_obj_create(scr);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, items[i].name);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
        s_ui.status[i] = lv_label_create(row);
        lv_obj_align(s_ui.status[i], LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_set_style_text_font(s_ui.status[i], &lv_font_montserrat_16, 0);
        lv_label_set_text(s_ui.status[i], ft_status_str(FT_PENDING));
        lv_obj_set_style_text_color(s_ui.status[i], status_color(FT_PENDING), 0);
    }
    s_ui.summary = lv_label_create(scr);
    lv_label_set_text(s_ui.summary, "");
    lv_obj_set_style_text_font(s_ui.summary, &lv_font_montserrat_24, 0);
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    lv_unlock();
}

static void ui_status(size_t i, ft_status_t st)
{
    if (!ui_ready() || i >= sizeof s_ui.status / sizeof s_ui.status[0] || !s_ui.status[i]) {
        return;
    }
    lv_lock();
    lv_label_set_text(s_ui.status[i], ft_status_str(st));
    lv_obj_set_style_text_color(s_ui.status[i], status_color(st), 0);
    lv_obj_scroll_to_view(s_ui.status[i], LV_ANIM_OFF);
    lv_unlock();
}

static void ui_prompt(const char *text)
{
    if (!ui_ready() || !s_ui.prompt) {
        return;
    }
    lv_lock();
    lv_label_set_text(s_ui.prompt, text);
    lv_unlock();
}

static void ui_summary(bool pass)
{
    if (!ui_ready() || !s_ui.summary) {
        return;
    }
    lv_lock();
    lv_label_set_text(s_ui.summary, pass ? "ALL PASS" : "FAILED");
    lv_obj_set_style_text_color(s_ui.summary, status_color(pass ? FT_PASS : FT_FAIL), 0);
    lv_obj_scroll_to_view(s_ui.summary, LV_ANIM_OFF);
    lv_unlock();
}

// --- Tests ------------------------------------------------------------------------

static ft_status_t t_i2c(char *d, size_t len)
{
    char missing[48] = "";
    int ok = 0;
    for (size_t i = 0; i < BSP_I2C_DEVICE_COUNT; i++) {
        const uint8_t addr = bsp_i2c_device_addr(i);
        if (i2c_master_probe(bsp_i2c_bus(), addr, I2C_PROBE_MS) == ESP_OK) {
            ok++;
        } else {
            const size_t used = strlen(missing);
            snprintf(missing + used, sizeof missing - used, " %s", bsp_i2c_device_name(addr));
        }
    }
    snprintf(d, len, "%d/%d%s%s", ok, BSP_I2C_DEVICE_COUNT, *missing ? " missing:" : "", missing);
    return ok == BSP_I2C_DEVICE_COUNT ? FT_PASS : FT_FAIL;
}

static ft_status_t t_display(char *d, size_t len)
{
    if (!bsp_display_panel()) {
        snprintf(d, len, "panel not initialised");
        return FT_FAIL;
    }
    // TE pulses (~60 Hz) prove the panel is powered, initialised and scanning. Poll the
    // line: the LVGL flush owns the TE interrupt.
    const int edges = bsp_display_te_count_edges(TE_WINDOW_MS);
    snprintf(d, len, "TE %d Hz", edges * 1000 / TE_WINDOW_MS);
    return edges >= TE_MIN_EDGES ? FT_PASS : FT_FAIL;
}

static ft_status_t t_touch(char *d, size_t len)
{
    uint8_t id = 0;
    uint8_t fw = 0;
    if (!bsp_touch_handle() || ft3168_read_id(bsp_touch_handle(), &id, &fw) != ESP_OK) {
        snprintf(d, len, "no response");
        return FT_FAIL;
    }
    snprintf(d, len, "id 0x%02X fw 0x%02X", id, fw);
    return id == 0x03 ? FT_PASS : FT_FAIL;
}

static ft_status_t t_rtc(char *d, size_t len)
{
    pcf85063_handle_t rtc = bsp_rtc_handle();
    struct tm a;
    struct tm b;
    bool os = false;
    if (!rtc || pcf85063_get_time(rtc, &a, &os) != ESP_OK) {
        snprintf(d, len, "no response");
        return FT_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(1100));
    if (pcf85063_get_time(rtc, &b, NULL) != ESP_OK) {
        snprintf(d, len, "read failed");
        return FT_FAIL;
    }
    const int64_t dt = pcf85063_tm_to_unix(&b) - pcf85063_tm_to_unix(&a);
    snprintf(d, len, "ticks %llds%s", dt, os ? ", time never set" : "");
    return dt >= 1 && dt <= 2 ? FT_PASS : FT_FAIL;
}

static ft_status_t t_pmu(char *d, size_t len)
{
    axp2101_handle_t pmu = bsp_pmu_handle();
    axp2101_status_t st;
    axp2101_charger_cfg_t cc;
    if (!pmu || axp2101_read_status(pmu, &st) != ESP_OK || axp2101_charger_get(pmu, &cc) != ESP_OK) {
        snprintf(d, len, "no response");
        return FT_FAIL;
    }
    snprintf(d, len, "bat %d%% %umV%s, CC %umA", st.battery_pct, st.vbat_mv, st.vbus_good ? ", USB" : "", cc.cc_ma);
    const bool bat_ok = st.battery_present && st.vbat_mv >= 3000 && st.vbat_mv <= 4400;
    return bat_ok && cc.cc_ma <= 200 ? FT_PASS : FT_FAIL;
}

static ft_status_t imu_check(char *d, size_t len)
{
    qmi8658_handle_t imu = bsp_imu_handle();
    bool acc_ok = false;
    bool gyr_ok = false;
    int32_t mg[3];
    int32_t dps[3];
    if (!imu || qmi8658_reset(imu) != ESP_OK || qmi8658_selftest_accel(imu, &acc_ok, mg) != ESP_OK ||
        qmi8658_reset(imu) != ESP_OK || qmi8658_selftest_gyro(imu, &gyr_ok, dps) != ESP_OK ||
        qmi8658_reset(imu) != ESP_OK) {
        snprintf(d, len, "no response / self-test timeout");
        return FT_FAIL;
    }
    int32_t a[3] = {0};
    if (qmi8658_config_accel(imu, QMI8658_ACC_4G, QMI8658_ODR_62_5HZ) == ESP_OK &&
        qmi8658_enable(imu, true, false) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100));
        qmi8658_read(imu, a, NULL);
    }
    qmi8658_reset(imu); // sensors off again
    const int g_mg = (int)sqrtf((float)a[0] * a[0] + (float)a[1] * a[1] + (float)a[2] * a[2]);
    snprintf(d, len, "ST acc %s gyr %s, |a| %d mg", acc_ok ? "ok" : "FAIL", gyr_ok ? "ok" : "FAIL", g_mg);
    return acc_ok && gyr_ok && g_mg > 850 && g_mg < 1150 ? FT_PASS : FT_FAIL;
}

static ft_status_t t_imu(char *d, size_t len)
{
    svc_sensors_suspend(true); // raise to wake gives up the IMU (fails if svc_sensors is not running)
    const ft_status_t st = imu_check(d, len);
    svc_sensors_suspend(false);
    return st;
}

static ft_status_t t_audio(char *d, size_t len)
{
    if (!bsp_audio_ready()) {
        snprintf(d, len, "codec not initialised");
        return FT_FAIL;
    }
    audio_dsp_detect_t r[2];
    if (drv_audio_loopback(LOOPBACK_HZ, r) != ESP_OK) {
        snprintf(d, len, "codec I/O error");
        return FT_FAIL;
    }
    snprintf(d, len, "1 kHz SNR mic1 %.0f dB, mic2 %.0f dB", r[0].snr_db, r[1].snr_db);
    return r[0].detected && r[1].detected ? FT_PASS : FT_FAIL;
}

static ft_status_t file_roundtrip(const char *path, char *d, size_t len)
{
    static const char k_pattern[] = "S3Wear factory test 0123456789";
    char back[sizeof k_pattern] = {0};
    FILE *f = fopen(path, "w");
    bool ok = f && fwrite(k_pattern, 1, sizeof k_pattern, f) == sizeof k_pattern;
    ok = f && fclose(f) == 0 && ok;
    f = ok ? fopen(path, "r") : NULL;
    ok = f && fread(back, 1, sizeof back, f) == sizeof back && memcmp(back, k_pattern, sizeof back) == 0;
    if (f) {
        fclose(f);
    }
    unlink(path);
    snprintf(d, len, ok ? "write/read ok" : "write/read failed");
    return ok ? FT_PASS : FT_FAIL;
}

static ft_status_t t_flash(char *d, size_t len)
{
    return file_roundtrip(SVC_STORAGE_FLASH_PATH "/ft.tmp", d, len);
}

static ft_status_t t_sd(char *d, size_t len)
{
    if (!svc_storage_sd_check() && svc_storage_sd_mount() != ESP_OK) {
        snprintf(d, len, "no card");
        return FT_SKIP; // the TF card is optional hardware
    }
    return file_roundtrip(SVC_STORAGE_SD_PATH "/ft.tmp", d, len);
}

static ft_status_t t_ble(char *d, size_t len)
{
    return ft_ble_advertise(BLE_ADV_MS, d, len);
}

static ft_status_t t_wifi(char *d, size_t len)
{
    return ft_wifi_scan(d, len);
}

static ft_status_t wait_for(const char *prompt, bool (*pressed)(void), char *d, size_t len)
{
    ui_prompt(prompt);
    printf("%s (30 s)\n", prompt);
    const int64_t t0 = esp_timer_get_time();
    while (esp_timer_get_time() - t0 < (int64_t)INTERACT_TIMEOUT_MS * 1000) {
        if (pressed()) {
            ui_prompt("");
            snprintf(d, len, "after %lld ms", (esp_timer_get_time() - t0) / 1000);
            return FT_PASS;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ui_prompt("");
    snprintf(d, len, "timeout");
    return FT_FAIL;
}

static bool boot_pressed(void)
{
    return bsp_button_is_pressed(BSP_BUTTON_BOOT);
}

static bool pwr_pressed(void)
{
    return bsp_button_is_pressed(BSP_BUTTON_PWR);
}

static bool screen_touched(void)
{
    uint16_t x;
    uint16_t y;
    return bsp_touch_get(&x, &y);
}

static ft_status_t t_btn_boot(char *d, size_t len)
{
    return wait_for("Press BOOT", boot_pressed, d, len);
}

static ft_status_t t_btn_pwr(char *d, size_t len)
{
    return wait_for("Press PWR briefly", pwr_pressed, d, len);
}

static ft_status_t t_tap(char *d, size_t len)
{
    return bsp_touch_handle() ? wait_for("Tap the screen", screen_touched, d, len) : FT_FAIL;
}

static const ft_item_t k_items[] = {
    {"i2c", t_i2c, false},         {"display", t_display, false}, {"touch", t_touch, false},
    {"rtc", t_rtc, false},         {"pmu", t_pmu, false},         {"imu", t_imu, false},
    {"audio", t_audio, false},     {"flash", t_flash, false},     {"sd", t_sd, false},
    {"ble", t_ble, false},         {"wifi", t_wifi, false},       {"btn_boot", t_btn_boot, true},
    {"btn_pwr", t_btn_pwr, true},  {"touch_tap", t_tap, true},
};
#define N_ITEMS (sizeof k_items / sizeof k_items[0])

// --- Runner -----------------------------------------------------------------------

static ft_result_t s_results[N_ITEMS];
static bool s_interactive;
static SemaphoreHandle_t s_done;

static void ft_task(void *arg)
{
    (void)arg;
    for (size_t i = 0; i < N_ITEMS; i++) {
        s_results[i].name = k_items[i].name;
        s_results[i].status = FT_PENDING;
        s_results[i].detail[0] = '\0';
    }
    ui_build(k_items, N_ITEMS);
    for (size_t i = 0; i < N_ITEMS; i++) {
        ft_result_t *r = &s_results[i];
        if (k_items[i].interactive && !s_interactive) {
            r->status = FT_SKIP;
            snprintf(r->detail, sizeof r->detail, "non-interactive run");
        } else {
            r->status = FT_RUNNING;
            ui_status(i, FT_RUNNING);
            r->status = k_items[i].fn(r->detail, sizeof r->detail);
        }
        ui_status(i, r->status);
        ESP_LOGI(TAG, "%-9s %-4s %s", r->name, ft_status_str(r->status), r->detail);
    }
    ui_summary(ft_overall_pass(s_results, N_ITEMS));
    // Budget check (docs/02 §6: free internal heap must stay > 40 KB).
    ESP_LOGI(TAG, "internal heap: %u B free, %u B minimum since boot; PSRAM %u B free",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    xSemaphoreGive(s_done);
    vTaskDelete(NULL);
}

esp_err_t factory_test_run(bool interactive)
{
    if (!s_done) {
        s_done = xSemaphoreCreateBinary();
        ESP_RETURN_ON_FALSE(s_done, ESP_ERR_NO_MEM, TAG, "sem");
    }
    s_interactive = interactive;
    BaseType_t ok = xTaskCreate(ft_task, "factory_test", FT_TASK_STACK, NULL, FT_TASK_PRIO, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    xSemaphoreTake(s_done, portMAX_DELAY);

    char *json = heap_caps_malloc(JSON_MAX, MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(json, ESP_ERR_NO_MEM, TAG, "json buf");
    ft_json_write(json, JSON_MAX, esp_app_get_description()->version, s_results, N_ITEMS);
    printf("%s\n", json);
    heap_caps_free(json);
    return ft_overall_pass(s_results, N_ITEMS) ? ESP_OK : ESP_FAIL;
}
