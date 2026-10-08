// Console: `lcd bright|on|off|bars|demo|fps|te` — AMOLED + LVGL bring-up checks (P1-02);
// `lcd bench scroll|swipe` — display performance (P2-04).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp_s3w.h"
#include "demos/lv_demos.h"
#include "esp_console.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "s3w_lvgl_port.h"
#include "s3w_ui.h"
#include "sdkconfig.h"
#include "src/core/lv_obj_scroll_private.h" // lv_obj_scroll_by_raw
#include "svc_diag_priv.h"

#define FPS_DEFAULT_FRAMES   60
#define BENCH_DEFAULT_FRAMES 300
#define BENCH_TIMEOUT_MS     60000
#define BENCH_LIST_ITEMS     40
#define BENCH_SCROLL_STEP    6  // px per frame: a steady finger drag
#define BENCH_SWIPE_STEPS    16 // frames per full-width page swipe (~270 ms at 60 fps)

static void show_colour_bars(void)
{
    static const uint32_t k_bars[] = {0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000};
    const int n = sizeof k_bars / sizeof k_bars[0];

    lv_lock();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    const int32_t w = lv_display_get_horizontal_resolution(NULL);
    const int32_t h = lv_display_get_vertical_resolution(NULL);
    for (int i = 0; i < n; i++) {
        lv_obj_t *bar = lv_obj_create(scr);
        lv_obj_remove_style_all(bar);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(k_bars[i]), 0);
        lv_obj_set_pos(bar, i * w / n, 0);
        lv_obj_set_size(bar, (i + 1) * w / n - i * w / n, h);
    }
    // 3 px red frame on the outermost pixels: all four edges must be visible and equally thick,
    // otherwise the panel gap (column/row offset) is wrong.
    lv_obj_t *frame = lv_obj_create(scr);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, w, h);
    lv_obj_set_style_border_width(frame, 3, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_border_opa(frame, LV_OPA_COVER, 0);
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    lv_unlock();
}

static int measure_fps(int frames)
{
    lv_display_t *disp = s3w_lvgl_port_display();
    lv_lock();
    lv_obj_t *o = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_unlock();

    const int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < frames; i++) {
        lv_lock();
        lv_obj_set_style_bg_color(o, lv_color_hex(i & 1 ? 0x202080 : 0x802020), 0);
        lv_refr_now(disp);
        lv_unlock();
    }
    const int64_t us = esp_timer_get_time() - t0;

    lv_lock();
    lv_obj_delete(o);
    lv_unlock();

    const int fps_x10 = (int)((int64_t)frames * 10000000 / us);
    printf("full-screen refresh: %d frames in %lld ms -> %d.%d fps\n", frames, us / 1000, fps_x10 / 10, fps_x10 % 10);
    return 0;
}

// --- lcd bench ------------------------------------------------------------------
// Runs on the UI task (via the mailbox) like real UI work: build a scene, then
// move it one step and lv_refr_now() per frame, back and forth, `frames` times.

typedef enum { BENCH_SCROLL, BENCH_SWIPE } bench_kind_t;

typedef struct {
    bench_kind_t kind;
    int frames;
    SemaphoreHandle_t done;
    int64_t us;
    int64_t update_us; // moving the scene (scroll call), outside lv_refr_now()
    s3w_lvgl_port_stats_t st;
} bench_t;

static lv_obj_t *bench_list(lv_obj_t *scr)
{
    static const char *const k_icons[] = {LV_SYMBOL_BELL, LV_SYMBOL_BLUETOOTH, LV_SYMBOL_SETTINGS,
                                          LV_SYMBOL_BATTERY_3, LV_SYMBOL_AUDIO, LV_SYMBOL_GPS};
    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    // Full-screen list as the watch theme will draw it: square, no corner clipping.
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_set_style_clip_corner(list, false, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_anim_duration(list, 0, 0);
    for (int i = 0; i < BENCH_LIST_ITEMS; i++) {
        char txt[24];
        snprintf(txt, sizeof txt, "List item %d", i + 1);
        lv_list_add_button(list, k_icons[i % (sizeof k_icons / sizeof k_icons[0])], txt);
    }
    return list;
}

static lv_obj_t *bench_pages(lv_obj_t *scr)
{
    static const uint32_t k_bg[] = {0x10306A, 0x6A2010};
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *page = lv_obj_create(row);
        lv_obj_remove_style_all(page);
        lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(page, lv_color_hex(k_bg[i]), 0);
        lv_obj_set_style_bg_grad_color(page, lv_color_black(), 0);
        lv_obj_set_style_bg_grad_dir(page, LV_GRAD_DIR_VER, 0);
        lv_obj_t *time = lv_label_create(page);
        lv_label_set_text(time, i ? "10:43" : "10:42");
        lv_obj_set_style_text_font(time, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(time, lv_color_white(), 0);
        lv_obj_align(time, LV_ALIGN_CENTER, 0, -60);
        for (int b = 0; b < 3; b++) {
            lv_obj_t *btn = lv_button_create(page);
            lv_obj_set_size(btn, 100, 60);
            lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, (b - 1) * 120, -60);
        }
    }
    return row;
}

static void bench_run(void *ctx)
{
    bench_t *b = ctx;
    lv_display_t *disp = s3w_lvgl_port_display();
    lv_obj_t *prev = lv_screen_active();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_t *obj = b->kind == BENCH_SCROLL ? bench_list(scr) : bench_pages(scr);
    lv_screen_load(scr);
    lv_refr_now(disp);
    s3w_lvgl_port_stats(NULL, true);

    // Scroll ranges are measured once: querying them per frame walks every child.
    const int32_t range = b->kind == BENCH_SCROLL ? lv_obj_get_scroll_bottom(obj) : lv_obj_get_width(obj);
    const int32_t step = b->kind == BENCH_SCROLL ? BENCH_SCROLL_STEP
                                                 : (range + BENCH_SWIPE_STEPS - 1) / BENCH_SWIPE_STEPS;
    int32_t pos = 0;
    int dir = 1;
    b->update_us = 0;
    // Like a finger drag (lv_indev_scroll.c): SCROLL_BEGIN once, raw steps, SCROLL_END.
    // lv_obj_scroll_by(LV_ANIM_OFF) would toggle LV_STATE_SCROLLED every step, and each
    // toggle restyles and re-lays-out every child (+11 ms/frame for 40 list items).
    lv_obj_send_event(obj, LV_EVENT_SCROLL_BEGIN, NULL);
    const int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < b->frames; i++) {
        const int64_t t_update = esp_timer_get_time();
        if ((dir > 0 && pos >= range) || (dir < 0 && pos <= 0)) {
            dir = -dir;
        }
        const int32_t next = LV_CLAMP(0, pos + dir * step, range);
        if (b->kind == BENCH_SCROLL) {
            lv_obj_scroll_by_raw(obj, 0, pos - next);
        } else {
            lv_obj_scroll_by_raw(obj, pos - next, 0);
        }
        pos = next;
        b->update_us += esp_timer_get_time() - t_update;
        lv_refr_now(disp);
    }
    b->us = esp_timer_get_time() - t0;
    s3w_lvgl_port_stats(&b->st, false);
    lv_obj_send_event(obj, LV_EVENT_SCROLL_END, NULL);

    lv_screen_load(prev);
    lv_obj_delete(scr);
    xSemaphoreGive(b->done);
}

static void print_ms(const char *what, uint64_t us, int frames)
{
    const int x100 = (int)(us / 10 / (uint64_t)frames); // 1/100 ms per frame
    printf("  %-24s %3d.%02d ms\n", what, x100 / 100, x100 % 100);
}

static int run_bench(bench_kind_t kind, int frames)
{
    static bench_t b;      // lives on: the UI task may still use it after a timeout
    static bool s_stuck;   // a timed-out run never handed b back
    if (s_stuck) {
        printf("previous bench never finished\n");
        return 1;
    }
    if (!b.done && !(b.done = xSemaphoreCreateBinary())) {
        return 1;
    }
    b.kind = kind;
    b.frames = frames;
    if (s3w_ui_post(bench_run, &b) != ESP_OK) {
        printf("UI mailbox full\n");
        return 1;
    }
    if (xSemaphoreTake(b.done, pdMS_TO_TICKS(BENCH_TIMEOUT_MS)) != pdTRUE) {
        s_stuck = true;
        printf("bench did not finish\n");
        return 1;
    }

#if CONFIG_S3W_LVGL_RENDER_DIRECT
    const char *mode = "direct";
#else
    const char *mode = "partial";
#endif
#if CONFIG_COMPILER_OPTIMIZATION_PERF
    const char *opt = "-O2";
#elif CONFIG_COMPILER_OPTIMIZATION_SIZE
    const char *opt = "-Os";
#else
    const char *opt = "-Og, LVGL -O2";
#endif
    const s3w_lvgl_port_stats_t *st = &b.st;
    const int fps_x10 = (int)((int64_t)frames * 10000000 / b.us);
    const uint64_t other = b.us - (int64_t)(st->te_wait_us + st->swap_us + st->bus_wait_us) - b.update_us;
    printf("bench %s (%s, QSPI %d MHz, %s): %d frames in %lld ms -> %d.%d fps\n",
           kind == BENCH_SCROLL ? "scroll" : "swipe", mode,
           CONFIG_S3W_LCD_PCLK_MHZ, opt, frames, b.us / 1000, fps_x10 / 10, fps_x10 % 10);
    printf("  per frame: %u px in %u.%u flushes, %u flushed frames\n", (unsigned)(st->px / (uint64_t)frames),
           (unsigned)(st->flushes * 10 / frames / 10), (unsigned)(st->flushes * 10 / frames % 10),
           (unsigned)st->frames);
    print_ms("total", b.us, frames);
    print_ms("scene update", b.update_us, frames);
    print_ms("TE wait", st->te_wait_us, frames);
    print_ms("swap / copy", st->swap_us, frames);
    print_ms("bus wait (draw_bitmap)", st->bus_wait_us, frames);
    print_ms("render + LVGL", other, frames);
    return 0;
}

esp_err_t diag_panel_power(bool on)
{
    esp_lcd_panel_handle_t panel = bsp_display_panel();
    // Sleep out/in (0x11/0x10) plus display on/off (0x29/0x28). Hold the LVGL lock
    // so no flush runs while the panel changes state.
    lv_lock();
    esp_err_t err = on ? esp_lcd_panel_disp_sleep(panel, false) : esp_lcd_panel_disp_on_off(panel, false);
    if (err == ESP_OK) {
        err = on ? esp_lcd_panel_disp_on_off(panel, true) : esp_lcd_panel_disp_sleep(panel, true);
    }
    lv_unlock();
    return err;
}

static int cmd_lcd(int argc, char **argv)
{
    if (!s3w_lvgl_port_display()) {
        printf("display not initialised\n");
        return 1;
    }
    const char *sub = argc >= 2 ? argv[1] : "";
    if (strcmp(sub, "bright") == 0 && argc == 3) {
        const int level = atoi(argv[2]);
        if (level < 0 || level > 255) {
            printf("level must be 0..255\n");
            return 1;
        }
        return bsp_display_brightness_set((uint8_t)level) == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "on") == 0 || strcmp(sub, "off") == 0) {
        return diag_panel_power(sub[1] == 'n') == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "te") == 0) {
        const int edges = bsp_display_te_count_edges(1000);
        const esp_err_t err = bsp_display_te_wait(100);
        printf("TE: %d rising edges in 1 s (expect ~60), interrupt wait: %s\n", edges, esp_err_to_name(err));
        return edges > 0 && err == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "bars") == 0) {
        show_colour_bars();
        return 0;
    }
    if (strcmp(sub, "demo") == 0) {
#if LV_USE_DEMO_WIDGETS
        lv_lock();
        // Own screen: the active one belongs to ui_nav (`ui home` brings it back).
        lv_screen_load_anim(lv_obj_create(NULL), LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
        lv_demo_widgets();
        lv_unlock();
        return 0;
#else
        printf("widgets demo not built (release profile)\n");
        return 1;
#endif
    }
    if (strcmp(sub, "fps") == 0) {
        const int frames = argc >= 3 ? atoi(argv[2]) : FPS_DEFAULT_FRAMES;
        return measure_fps(frames > 0 ? frames : FPS_DEFAULT_FRAMES);
    }
    if (strcmp(sub, "bench") == 0 && argc >= 3 && (strcmp(argv[2], "scroll") == 0 || strcmp(argv[2], "swipe") == 0)) {
        const int frames = argc >= 4 ? atoi(argv[3]) : BENCH_DEFAULT_FRAMES;
        return run_bench(argv[2][1] == 'c' ? BENCH_SCROLL : BENCH_SWIPE, frames > 0 ? frames : BENCH_DEFAULT_FRAMES);
    }
    printf("usage: lcd bright <0..255> | on | off | bars | demo | fps [frames] | te | bench scroll|swipe [frames]\n");
    return 1;
}

esp_err_t diag_register_lcd(void)
{
    const esp_console_cmd_t cmd = {
        .command = "lcd",
        .help = "AMOLED: bright <0..255>, on/off (sleep out/in), bars, demo (LVGL widgets), fps [frames], te, "
                "bench scroll|swipe [frames] (fps + per-frame time split)",
        .hint = "bright|on|off|bars|demo|fps|te|bench",
        .func = cmd_lcd,
    };
    return esp_console_cmd_register(&cmd);
}
