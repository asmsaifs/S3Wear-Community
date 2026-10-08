// Console: `face [list] | face <id> | face aod on|off | face slot <n> <complication>
// | face config` — watch face engine (P3-02), picker/customize configuration (P3-04).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "s3w_lvgl_port.h"
#include "svc_diag_priv.h"
#include "svc_settings.h"
#include "wf_engine.h"

static void print_faces(void)
{
    lv_lock();
    const wf_face_def_t *active = wf_active();
    const bool aod = wf_is_aod();
    for (size_t i = 0; i < wf_count(); i++) {
        const wf_face_def_t *f = wf_at(i);
        printf("%c %-10s %-16s", f == active ? '*' : ' ', f->id, f->name);
        for (uint8_t s = 0; s < f->slot_count; s++) {
            printf(" %u:%s=%s", s, f->slots[s].id, wf_comp_info(wf_get_slot(f, s))->id);
        }
        if (wf_get_color(f) != WF_COLOR_DEFAULT) {
            printf(" color=%06lx", (unsigned long)wf_get_color(f));
        }
        printf("\n");
    }
    lv_unlock();
    printf("AOD variant: %s\ncomplications:", aod ? "on" : "off");
    for (int c = 0; c < WF_COMP_COUNT; c++) {
        printf(" %s", wf_comp_info((wf_comp_t)c)->id);
    }
    printf("\n");
}

/** Run fn under lv_lock and draw the frame; prints the time (build + render). */
static esp_err_t timed(const char *what, esp_err_t (*fn)(const void *arg), const void *arg)
{
    lv_lock();
    const int64_t t0 = esp_timer_get_time();
    const esp_err_t err = fn(arg);
    if (err == ESP_OK) {
        lv_refr_now(NULL);
    }
    const int64_t us = esp_timer_get_time() - t0;
    lv_unlock();
    printf("%s: %s, %lld.%lld ms (build + one frame)\n", what, esp_err_to_name(err), us / 1000, (us % 1000) / 100);
    return err;
}

static esp_err_t set_face(const void *id)
{
    return wf_set_active(id);
}

static esp_err_t set_aod(const void *on)
{
    wf_set_aod(*(const bool *)on);
    return ESP_OK;
}

typedef struct {
    uint8_t slot;
    wf_comp_t comp;
} slot_arg_t;

static esp_err_t set_slot(const void *arg)
{
    const slot_arg_t *a = arg;
    return wf_set_slot(wf_active()->id, a->slot, a->comp);
}

static int cmd_face(int argc, char **argv)
{
    if (!s3w_lvgl_port_display()) {
        printf("UI task not running\n");
        return 1;
    }
    const char *sub = argc >= 2 ? argv[1] : "list";
    if (strcmp(sub, "list") == 0) {
        print_faces();
        return 0;
    }
    if (strcmp(sub, "aod") == 0 && argc >= 3) {
        // Preview only: svc_power sets it back on the next screen state change.
        const bool on = strcmp(argv[2], "on") == 0;
        return timed(on ? "AOD variant" : "normal variant", set_aod, &on) == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "slot") == 0 && argc >= 4) {
        slot_arg_t a = {.slot = (uint8_t)atoi(argv[2]), .comp = wf_comp_find(argv[3])};
        if (strcmp(argv[3], "default") != 0 && a.comp == WF_COMP_COUNT) {
            printf("unknown complication '%s' (see 'face list')\n", argv[3]);
            return 1;
        }
        return timed("slot", set_slot, &a) == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "config") == 0) {
        static char cfg[512]; // console task only
        lv_lock();
        const esp_err_t err = wf_config_save(cfg, sizeof cfg);
        lv_unlock();
        printf("in use: '%s'%s\n", cfg, err == ESP_OK ? "" : " (truncated)");
        if (svc_settings_get_str(S3W_SETTING_FACE_CONFIG, cfg, sizeof cfg) == ESP_OK) {
            printf("saved:  '%s'\n", cfg);
        }
        return 0;
    }
    if (argc == 2) {
        if (timed(sub, set_face, sub) != ESP_OK) {
            return 1;
        }
        const esp_err_t err = svc_settings_set_str(S3W_SETTING_WATCH_FACE, sub);
        if (err != ESP_OK) {
            printf("not saved: %s\n", esp_err_to_name(err));
        }
        return 0;
    }
    printf("usage: face [list] | face <id> | face aod on|off | face slot <n> <complication|default> | face config\n");
    return 1;
}

esp_err_t diag_register_face(void)
{
    const esp_console_cmd_t cmd = {
        .command = "face",
        .help = "watch faces: list (faces, slots, complications), <id> (switch, saved, timed), "
                "aod on|off (preview the AOD variant), slot <n> <complication|default> (active face, not saved), "
                "config (slots and colours in use and saved, FACE_CONFIG)",
        .hint = "[list|<id>|aod on|off|slot <n> <comp>|config]",
        .func = cmd_face,
    };
    return esp_console_cmd_register(&cmd);
}
