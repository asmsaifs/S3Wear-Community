// Console: `touch info|draw|monitor` — FT3168 bring-up checks (P1-03).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp_s3w.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "s3w_lvgl_port.h"
#include "svc_diag_priv.h"

static const char *TAG = "cmd_touch";

#define MONITOR_DEFAULT_S 30
#define DOT_RADIUS        3

// --- touch draw: canvas test app ---------------------------------------------

typedef struct {
    lv_obj_t *canvas;
    lv_obj_t *label;
    lv_draw_buf_t *buf;
} draw_app_t;

static const char *dir_name(lv_dir_t dir)
{
    switch (dir) {
    case LV_DIR_LEFT:
        return "left";
    case LV_DIR_RIGHT:
        return "right";
    case LV_DIR_TOP:
        return "up";
    case LV_DIR_BOTTOM:
        return "down";
    default:
        return "none";
    }
}

static void draw_event_cb(lv_event_t *e)
{
    draw_app_t *app = lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSING: {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        const int32_t w = lv_obj_get_width(app->canvas);
        const int32_t h = lv_obj_get_height(app->canvas);
        for (int32_t dy = -DOT_RADIUS; dy <= DOT_RADIUS; dy++) {
            for (int32_t dx = -DOT_RADIUS; dx <= DOT_RADIUS; dx++) {
                const int32_t x = p.x + dx;
                const int32_t y = p.y + dy;
                if (x >= 0 && y >= 0 && x < w && y < h) {
                    lv_canvas_set_px(app->canvas, x, y, lv_color_hex(0x00C8FF), LV_OPA_COVER);
                }
            }
        }
        lv_obj_invalidate(app->canvas);
        break;
    }
    case LV_EVENT_GESTURE: {
        const char *d = dir_name(lv_indev_get_gesture_dir(indev));
        // A swipe ends this press: no long-press or click may follow it.
        lv_indev_wait_release(indev);
        ESP_LOGI(TAG, "swipe %s", d);
        lv_label_set_text_fmt(app->label, "swipe %s", d);
        break;
    }
    case LV_EVENT_LONG_PRESSED: {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        ESP_LOGI(TAG, "long-press at %d,%d", (int)p.x, (int)p.y);
        lv_label_set_text_fmt(app->label, "long-press %d,%d", (int)p.x, (int)p.y);
        break;
    }
    case LV_EVENT_SHORT_CLICKED:
        ESP_LOGI(TAG, "tap");
        break;
    case LV_EVENT_DELETE:
        lv_draw_buf_destroy(app->buf);
        lv_free(app);
        break;
    default:
        break;
    }
}

static int touch_draw(void)
{
    lv_lock();
    const int32_t w = lv_display_get_horizontal_resolution(NULL);
    const int32_t h = lv_display_get_vertical_resolution(NULL);
    draw_app_t *app = lv_malloc_zeroed(sizeof *app);
    app->buf = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO); // lv_malloc -> PSRAM
    if (!app->buf) {
        lv_free(app);
        lv_unlock();
        printf("no memory for canvas\n");
        return 1;
    }
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    app->canvas = lv_canvas_create(scr);
    lv_canvas_set_draw_buf(app->canvas, app->buf);
    lv_canvas_fill_bg(app->canvas, lv_color_black(), LV_OPA_COVER);
    app->label = lv_label_create(scr);
    lv_label_set_text(app->label, "draw / swipe / long-press");
    lv_obj_set_style_text_color(app->label, lv_color_white(), 0);
    lv_obj_align(app->label, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_add_event_cb(scr, draw_event_cb, LV_EVENT_ALL, app);
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    lv_unlock();
    printf("touch draw test running; swipes and long-presses are logged\n");
    return 0;
}

// --- touch monitor: tap-to-wake test -------------------------------------------

static SemaphoreHandle_t s_wake_sem;
static volatile int64_t s_tap_us;

static void on_touch_down(uint16_t x, uint16_t y, void *ctx)
{
    (void)x;
    (void)y;
    (void)ctx;
    s_tap_us = esp_timer_get_time();
    xSemaphoreGive(s_wake_sem);
}

static int touch_monitor(int timeout_s)
{
    ft3168_handle_t tp = bsp_touch_handle();
    if (!s3w_lvgl_port_display()) {
        printf("display not initialised\n");
        return 1;
    }
    if (!s_wake_sem) {
        s_wake_sem = xSemaphoreCreateBinary();
    }
    xSemaphoreTake(s_wake_sem, 0);

    if (diag_panel_power(false) != ESP_OK || ft3168_set_power_mode(tp, FT3168_PMODE_MONITOR) != ESP_OK) {
        printf("failed to enter monitor mode\n");
        diag_panel_power(true);
        return 1;
    }
    bsp_touch_event_cb_t prev_cb;
    void *prev_ctx;
    bsp_touch_get_event_cb(&prev_cb, &prev_ctx);
    bsp_touch_set_event_cb(on_touch_down, NULL);
    printf("screen off, touch in monitor mode; tap within %d s\n", timeout_s);

    const bool woke = xSemaphoreTake(s_wake_sem, pdMS_TO_TICKS(timeout_s * 1000)) == pdTRUE;
    bsp_touch_set_event_cb(prev_cb, prev_ctx); // svc_power's tap-to-wake
    ft3168_set_power_mode(tp, FT3168_PMODE_ACTIVE);
    diag_panel_power(true);
    if (!woke) {
        printf("no tap: timeout\n");
        return 1;
    }
    printf("woke by tap: screen on %lld ms after touch-down\n", (esp_timer_get_time() - s_tap_us) / 1000);
    return 0;
}

static int touch_info(void)
{
    ft3168_handle_t tp = bsp_touch_handle();
    uint8_t id = 0;
    uint8_t fw = 0;
    ft3168_pmode_t mode = FT3168_PMODE_ACTIVE;
    if (ft3168_read_id(tp, &id, &fw) != ESP_OK || ft3168_get_power_mode(tp, &mode) != ESP_OK) {
        printf("FT3168 not responding\n");
        return 1;
    }
    uint16_t x = 0;
    uint16_t y = 0;
    const bool down = bsp_touch_get(&x, &y);
    printf("FT3168 id 0x%02X%s fw 0x%02X pmode %d, %s (%u,%u)\n", id, id == 0x03 ? " (FT3168)" : "", fw, (int)mode,
           down ? "pressed" : "released", x, y);
    return 0;
}

static int cmd_touch(int argc, char **argv)
{
    if (!bsp_touch_handle()) {
        printf("touch not initialised\n");
        return 1;
    }
    const char *sub = argc >= 2 ? argv[1] : "";
    if (strcmp(sub, "info") == 0) {
        return touch_info();
    }
    if (strcmp(sub, "draw") == 0) {
        return touch_draw();
    }
    if (strcmp(sub, "monitor") == 0) {
        const int s = argc >= 3 ? atoi(argv[2]) : MONITOR_DEFAULT_S;
        return touch_monitor(s > 0 ? s : MONITOR_DEFAULT_S);
    }
    printf("usage: touch info | draw | monitor [seconds]\n");
    return 1;
}

esp_err_t diag_register_touch(void)
{
    const esp_console_cmd_t cmd = {
        .command = "touch",
        .help = "FT3168: info, draw (test app: draw, swipe, long-press), monitor [s] (screen off, tap to wake)",
        .hint = "info|draw|monitor",
        .func = cmd_touch,
    };
    return esp_console_cmd_register(&cmd);
}
