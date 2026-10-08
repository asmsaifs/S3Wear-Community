/* S3Wear desktop simulator.
 *
 *   s3w_sim                                    open a 410x502 SDL window (keys: see hal_sim.h)
 *   s3w_sim --screenshot out.png               render headless and write a PNG
 *   s3w_sim --script s.txt --screenshot o.png  run a scenario first (sim_script.c)
 *   ... --expect golden.png                    then fail unless o.png matches golden.png
 *   s3w_sim --faces <dir> [--face <id>]        also load <dir>/<id>/face.json, show one
 *   s3w_sim --app <name|file.wasm>             start a mini app after boot (sim_app.c)
 *   s3w_sim --dev <file.s3app>                 install + run a package, reload on change (s3w run-sim)
 *
 * Runs the same portable UI code as the watch (ui_framework/ui) over hal_sim.
 * Headless runs use a pinned clock (2026-10-03 10:09 UTC) so snapshots are stable.
 */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hal.h"
#include "hal_sim.h"
#include "battery_apps.h"
#include "clock_apps.h"
#include "lvgl.h"
#include "shell.h"
#if S3W_EDITION_PRO
#include "sim_app.h"
#endif
#include "sim_battery.h"
#include "sim_settings.h"
#include "sim_clock.h"
#include "s3w_edition.h"
#include "sim_connect.h"
#if S3W_EDITION_PRO
#include "sim_notify.h"
#include "sim_find.h"
#include "sim_flashlight.h"
#include "sim_call.h"
#include "sim_media.h"
#include "sim_ha.h"
#include "sim_memo.h"
#include "find_apps.h"
#include "call_apps.h"
#endif
#include "sim_data.h"
#include "sim_modes.h"
#include "sim_screenshot.h"
#include "sim_script.h"
#include "ui_nav.h"
#include "ui_root.h"
#include "ui_screens.h"
#include "ui_theme.h"
#include "wf_engine.h"

#define SIM_HOR_RES HAL_DISPLAY_HRES
#define SIM_VER_RES HAL_DISPLAY_VRES
#define BOOT_BRIGHTNESS 200
#define SETTLE_MS 500 /* boot fade-in and first layout before a script starts */

/* svc_input does not run in the simulator yet: BOOT = back (launcher on the face),
 * PWR = home (snooze while an alarm rings, mute an incoming call), on press. */
static void on_button(hal_button_t button, bool pressed, void *ctx)
{
    (void)ctx;
    printf("button %s %s\n", hal_button_name(button), pressed ? "down" : "up");
    if (!pressed) {
        return;
    }
    if (button == HAL_BUTTON_BACK) {
        if (!ui_nav_back()) {
            ui_nav_home_swipe(LV_DIR_RIGHT);
        }
    } else if (button == HAL_BUTTON_POWER) {
        if (clock_apps_ring_active()) {
            clock_apps_ring_key();
#if S3W_EDITION_PRO
        } else if (find_apps_watch_active()) {
            find_apps_watch_key();
        } else if (call_apps_key()) {
            // PWR muted the incoming call
#endif
        } else {
            ui_nav_home();
        }
    }
}

static void on_pmu(hal_pmu_event_t evt, void *ctx)
{
    (void)ctx;
    hal_battery_t b;
    hal_pmu_read_battery(&b);
    printf("pmu event %d: %d %%, %u mV, vbus %d, charging %d\n", (int)evt, b.percent, b.mv, b.vbus, b.charging);
    sim_data_battery();
    if (evt == HAL_PMU_EVT_VBUS_IN || evt == HAL_PMU_EVT_VBUS_OUT) {
        battery_apps_charger(evt == HAL_PMU_EVT_VBUS_IN); /* as app_main on SVC_POWER_EVT_BATTERY */
    }
    battery_apps_changed();
}

/* Quick settings backend, like app_main's but in memory (no svc_settings here);
 * DND, theater and sleep come from sim_modes, battery saver from sim_battery,
 * Bluetooth and Wi-Fi from sim_connect (shared with Settings > Connections). */
#define QS_MODES ((1u << SHELL_QS_DND) | (1u << SHELL_QS_THEATER) | (1u << SHELL_QS_SLEEP))

static shell_qs_state_t s_qs = {
    .available = QS_MODES | (1u << SHELL_QS_AOD) | (1u << SHELL_QS_SILENT) | (1u << SHELL_QS_SAVER) |
                 (1u << SHELL_QS_BLUETOOTH) | (1u << SHELL_QS_WIFI),
    .brightness = 60,
};

static void qs_read(shell_qs_state_t *st, void *ctx)
{
    (void)ctx;
    hal_battery_t b = {.percent = -1};
    hal_pmu_read_battery(&b);
    *st = s_qs;
    const modes_state_t m = sim_modes_state();
    st->on = (st->on & ~(QS_MODES | 1u << SHELL_QS_SAVER | 1u << SHELL_QS_BLUETOOTH | 1u << SHELL_QS_WIFI)) |
             (m.dnd ? 1u << SHELL_QS_DND : 0) | (m.theater ? 1u << SHELL_QS_THEATER : 0) |
             (m.sleep ? 1u << SHELL_QS_SLEEP : 0) | (sim_battery_saver() ? 1u << SHELL_QS_SAVER : 0) |
             (sim_connect_bluetooth() ? 1u << SHELL_QS_BLUETOOTH : 0) | (sim_connect_wifi() ? 1u << SHELL_QS_WIFI : 0);
    st->battery_pct = b.percent;
    st->charging = b.charging;
}

static void qs_toggle(shell_qs_item_t item, bool on, void *ctx)
{
    (void)ctx;
    printf("quick settings: %s %s\n", shell_qs_name(item), on ? "on" : "off");
    if (item == SHELL_QS_DND || item == SHELL_QS_THEATER || item == SHELL_QS_SLEEP) {
        sim_modes_set(item == SHELL_QS_DND ? MODE_DND : item == SHELL_QS_SLEEP ? MODE_SLEEP : MODE_THEATER, on);
        return;
    }
    if (item == SHELL_QS_SAVER) {
        sim_battery_set_saver(on);
        return;
    }
    if (item == SHELL_QS_BLUETOOTH) {
        sim_connect_set_bluetooth(on);
        return;
    }
    if (item == SHELL_QS_WIFI) {
        sim_connect_set_wifi(on);
        return;
    }
    s_qs.on = on ? s_qs.on | 1u << item : s_qs.on & ~(1u << item);
}

static void qs_brightness(uint8_t pct, void *ctx)
{
    (void)ctx;
    printf("quick settings: brightness %u %%\n", pct);
    s_qs.brightness = pct;
    hal_display_set_brightness((uint8_t)(pct * 255 / 100));
}

static const char *s_faces_dir; /* --faces: declarative faces to load at boot */
static const char *s_face_id;   /* --face: face to show */
static const char *s_app;       /* --app: mini app to start */
static const char *s_dev;       /* --dev: package to install and run (s3w run-sim) */
static bool s_headless;

static void face_report(const char *path, esp_err_t result, const wf_decl_err_t *err, void *ctx)
{
    (void)ctx;
    if (result == ESP_OK) {
        printf("face: loaded %s\n", path);
    } else if (err->line > 0) {
        fprintf(stderr, "face: %s:%d:%d: %s\n", path, err->line, err->col, err->msg);
    } else {
        fprintf(stderr, "face: %s: %s\n", path, err->msg);
    }
}

/* The parts of app_main's boot sequence that are not board bring-up. */
static void boot(lv_display_t *disp)
{
    time_t now = 0;
    bool valid = false;
    if (hal_rtc_get(&now, &valid) == ESP_OK && valid) {
        char buf[32];
        strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", gmtime(&now));
        printf("system time from RTC: %s UTC\n", buf);
    }
    ui_theme_init(disp);
    ui_boot_screen_show();
    hal_display_set_brightness(BOOT_BRIGHTNESS);
    ui_pointer_create(disp);
    hal_buttons_set_callback(on_button, NULL);
    hal_pmu_set_event_cb(on_pmu, NULL);
    wf_init();
    if (s_faces_dir != NULL) {
        wf_decl_load_dir(s_faces_dir, face_report, NULL);
    }
    if (s_face_id != NULL && wf_set_active(s_face_id) != ESP_OK) {
        fprintf(stderr, "face: unknown face '%s'\n", s_face_id);
    }
    shell_init();
    const shell_qs_backend_t qs = {.read = qs_read, .toggle = qs_toggle, .brightness = qs_brightness};
    shell_qs_set_backend(&qs);
    sim_battery_init();
    sim_settings_init();
    sim_data_battery();
    sim_data_demo();
    sim_clock_init();
#if S3W_EDITION_PRO
    sim_notify_init();
    sim_find_init();
    sim_flashlight_init();
    sim_media_init();
    sim_call_init();
#endif
    sim_connect_init();
#if S3W_EDITION_PRO
    sim_ha_init();
    sim_memo_init();
#endif
#if S3W_EDITION_PRO /* installable mini apps: Pro only */
    sim_app_init();
#endif
    ui_start();
#if S3W_EDITION_PRO
    if (s_app != NULL && !sim_app_run(s_app)) {
        fprintf(stderr, "app: cannot run '%s'\n", s_app);
    }
    /* In a window the package is watched: a rebuild reinstalls and restarts the app. */
    if (s_dev != NULL && !sim_app_dev(s_dev, !s_headless)) {
        fprintf(stderr, "app: cannot install '%s'\n", s_dev);
    }
#else
    if (s_app != NULL || s_dev != NULL) {
        fprintf(stderr, "--app / --dev: the Community edition has no mini apps\n");
    }
#endif
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [--script <file.txt>] [--screenshot <file.png> [--expect <golden.png>]]\n"
            "          [--faces <dir>] [--face <id>] [--app <name|file.wasm>] [--dev <file.s3app>]\n"
            "  --script <file>      run a scenario headless (firmware/test/ui/README.md)\n"
            "  --screenshot <file>  render headless (no window) and write a PNG at the end\n"
            "  --expect <file>      exit 1 unless the screenshot matches this PNG exactly\n"
            "  --faces <dir>        load declarative faces from <dir>/<id>/face.json\n"
            "  --face <id>          show this face (native, sample or loaded)\n"
            "  --app <name|file>    start a mini app: widgets (SDK sample), a test app or a .wasm\n"
            "  --dev <file.s3app>   install a package without asking and run it; in a window, reinstall\n"
            "                       and restart it whenever the file changes (s3w run-sim)\n",
            prog);
}

static int run_headless(const char *script, const char *png_path, const char *expect)
{
    /* Deterministic: UTC and a pinned clock. */
    setenv("TZ", "UTC0", 1);
    tzset();
    lv_display_t *disp = sim_headless_display_create(SIM_HOR_RES, SIM_VER_RES);
    if (disp == NULL) {
        fprintf(stderr, "failed to create headless display\n");
        return 1;
    }
    hal_sim_init(true);
    ui_clock_set_source(sim_fixed_now);
    boot(disp);
    sim_step(SETTLE_MS);
    if (script != NULL && !sim_script_run(script, disp)) {
        return 1;
    }
    if (png_path == NULL) {
        return 0;
    }
    if (!sim_screenshot_save(disp, png_path)) {
        return 1;
    }
    printf("wrote %s (%dx%d)\n", png_path, SIM_HOR_RES, SIM_VER_RES);
    if (expect != NULL) {
        const long diff = sim_png_compare(png_path, expect);
        if (diff != 0) {
            fprintf(stderr, "MISMATCH %s vs %s: %ld pixels differ\n", png_path, expect, diff);
            return 1;
        }
        printf("matches %s\n", expect);
    }
    return 0;
}

static int run_window(void)
{
    lv_display_t *disp = lv_sdl_window_create(SIM_HOR_RES, SIM_VER_RES);
    if (disp == NULL) {
        fprintf(stderr, "failed to create SDL window\n");
        return 1;
    }
    lv_sdl_window_set_title(disp, "S3Wear Simulator");
    hal_sim_init(false);
    boot(disp);

    /* LV_SDL_DIRECT_EXIT ends the process when the window is closed. */
    for (;;) {
        hal_sim_poll();
        uint32_t idle_ms = lv_timer_handler();
        SDL_Delay(idle_ms < 5 ? idle_ms : 5);
    }
}

int main(int argc, char **argv)
{
    const char *screenshot = NULL;
    const char *script = NULL;
    const char *expect = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            script = argv[++i];
        } else if (strcmp(argv[i], "--expect") == 0 && i + 1 < argc) {
            expect = argv[++i];
        } else if (strcmp(argv[i], "--faces") == 0 && i + 1 < argc) {
            s_faces_dir = argv[++i];
        } else if (strcmp(argv[i], "--face") == 0 && i + 1 < argc) {
            s_face_id = argv[++i];
        } else if (strcmp(argv[i], "--app") == 0 && i + 1 < argc) {
            s_app = argv[++i];
        } else if (strcmp(argv[i], "--dev") == 0 && i + 1 < argc) {
            s_dev = argv[++i];
        } else {
            usage(argv[0]);
            return strcmp(argv[i], "--help") == 0 ? 0 : 2;
        }
    }
    if (expect != NULL && screenshot == NULL) {
        usage(argv[0]);
        return 2;
    }

    lv_init();
    const bool headless = screenshot != NULL || script != NULL;
    s_headless = headless;
    return headless ? run_headless(script, screenshot, expect) : run_window();
}
