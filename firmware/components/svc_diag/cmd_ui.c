// Console: `ui test|stats|bad` — sys_core UI mailbox and event bus checks (P2-01);
// `ui nav|push|back|home|toast` — navigation stack and overlays (P2-05).
#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "s3w_lvgl_port.h"
#include "svc_diag_priv.h"
#include "sys_core.h"
#include "ui_nav.h"
#include "ui_overlay.h"
#include "ui_screens.h"

#define TEST_N 20

ESP_EVENT_DEFINE_BASE(DIAG_TEST_EVENT);

typedef struct {
    int next;       // next expected sequence number
    int errors;
    bool on_ui;     // every delivery ran on the UI task
    SemaphoreHandle_t done;
} order_t;

static void check(order_t *o, int seq)
{
    o->errors += seq != o->next;
    o->next = seq + 1;
    o->on_ui &= s3w_ui_is_ui_task();
    if (o->next == TEST_N) {
        xSemaphoreGive(o->done);
    }
}

static void on_mbox(void *ctx, int32_t arg, const void *data, size_t len)
{
    (void)data;
    (void)len;
    check(ctx, (int)arg);
}

static void on_event(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)base;
    (void)id;
    int seq = -1;
    if (len == sizeof seq) {
        memcpy(&seq, data, sizeof seq);
    }
    check(ctx, seq);
}

static bool run(const char *name, order_t *o, bool via_bus)
{
    *o = (order_t){.on_ui = true, .done = xSemaphoreCreateBinary()};
    s3w_event_sub_t sub = NULL;
    if (via_bus) {
        s3w_ui_subscribe(DIAG_TEST_EVENT, 0, on_event, o, &sub);
    }
    for (int i = 0; i < TEST_N; i++) {
        if (via_bus) {
            s3w_event_post(DIAG_TEST_EVENT, 0, &i, sizeof i);
        } else {
            s3w_ui_post_data(on_mbox, o, i, NULL, 0);
        }
    }
    const bool finished = xSemaphoreTake(o->done, pdMS_TO_TICKS(1000)) == pdTRUE;
    if (sub) {
        s3w_event_unsubscribe(sub);
    }
    vSemaphoreDelete(o->done);
    const bool pass = finished && o->errors == 0 && o->on_ui;
    printf("%-8s %d messages: %s (order errors %d, all on UI task: %s)\n", name, TEST_N, pass ? "PASS" : "FAIL",
           o->errors, o->on_ui ? "yes" : "no");
    return pass;
}

static int cmd_ui(int argc, char **argv)
{
    const char *sub = argc >= 2 ? argv[1] : "";
    if (strcmp(sub, "test") == 0) {
        if (!s3w_lvgl_port_display()) {
            printf("UI task not running\n");
            return 1;
        }
        order_t o;
        const bool a = run("mailbox", &o, false);
        const bool b = run("bus->ui", &o, true);
        return a && b ? 0 : 1;
    }
    if (strcmp(sub, "stats") == 0) {
        uint16_t hw = 0;
        uint32_t dropped = 0;
        s3w_ui_stats(&hw, &dropped);
        printf("UI mailbox: high-water %u of 32, dropped %lu\n", hw, (unsigned long)dropped);
        return 0;
    }
    if (strcmp(sub, "bad") == 0) {
        printf("touching LVGL from the console task without lv_lock(): debug builds abort\n");
        fflush(stdout);
        lv_obj_invalidate(lv_screen_active());
        printf("no abort: CONFIG_S3W_LVGL_THREAD_CHECK is off\n");
        return 0;
    }
    if (!s3w_lvgl_port_display()) {
        printf("UI task not running\n");
        return 1;
    }
    // Navigation commands: short LVGL calls from the console task, under lv_lock().
    if (strcmp(sub, "nav") == 0) {
        const char *ids[UI_NAV_MAX_DEPTH] = {0};
        lv_lock();
        const size_t depth = ui_nav_depth();
        for (size_t i = 0; i < depth; i++) {
            ids[i] = ui_screen_def(ui_nav_at(i))->id; // static strings
        }
        const bool active = ui_nav_is_active();
        const bool alert = ui_alert_is_active();
        lv_unlock();
        printf("stack (%u of %d, active %s, alert %s):\n", (unsigned)depth, UI_NAV_MAX_DEPTH, active ? "yes" : "no",
               alert ? "yes" : "no");
        for (size_t i = 0; i < depth; i++) {
            printf("  %u %s\n", (unsigned)i, ids[i]);
        }
        return 0;
    }
    if (strcmp(sub, "push") == 0 && argc >= 3) {
        lv_lock();
        const esp_err_t err = ui_nav_push_id(argv[2], NULL);
        lv_unlock();
        printf("push %s: %s\n", argv[2], esp_err_to_name(err));
        return err == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "back") == 0 || strcmp(sub, "home") == 0) {
        lv_lock();
        if (sub[0] == 'b') {
            ui_nav_back();
        } else if (ui_nav_depth() == 0) {
            // `lcd bars|demo` or the factory test replaced the UI: start it again.
            ui_start();
        } else {
            ui_nav_home();
        }
        lv_unlock();
        return 0;
    }
    if (strcmp(sub, "toast") == 0 && argc >= 3) {
        lv_lock();
        ui_toast_show(argv[2], 0);
        lv_unlock();
        return 0;
    }
    printf("usage: ui test | stats | bad | nav | push <id> | back | home | toast <text>\n");
    return 1;
}

esp_err_t diag_register_ui(void)
{
    const esp_console_cmd_t cmd = {
        .command = "ui",
        .help = "sys_core: test (mailbox + bus->UI ordering), stats, bad (deliberate off-thread LVGL call); "
                "navigation: nav (print stack), push <screen id>, back, home, toast <text>",
        .hint = "test|stats|bad|nav|push|back|home|toast",
        .func = cmd_ui,
    };
    return esp_console_cmd_register(&cmd);
}
