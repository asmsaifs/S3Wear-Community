/* Scenario scripts: one command per line, '#' starts a comment.
 *
 *   wait <ms>                      let time pass (animations, timers)
 *   tap <x> <y>                    press 80 ms, release, settle 100 ms
 *   hold <x> <y> [ms]              press ms (default 800: a long-press), release, settle 100 ms
 *   swipe <x1> <y1> <x2> <y2> [ms] drag in a straight line (default 200 ms)
 *   key back|power                 press and release a hardware button
 *   push <screen-id>               ui_nav_push_id()
 *   back | home                    ui_nav_back() / ui_nav_home()
 *   toast <text...>                ui_toast_show()
 *   clobber                        load a blank screen with auto-delete (like `lcd bars`)
 *   start                          ui_start() (console `ui home` after a clobber)
 *   time <HH:MM>                   pin the clock (UTC, the simulator's TZ is UTC0)
 *   time unknown                   "time unknown" state (svc_time: RTC lost power)
 *   shot <file.png>                screenshot now (the final --screenshot is separate)
 *   face <id>                      wf_set_active() (digital, analog, modular, minimal, s3w.neon, s3w.dial)
 *   aod on|off                     wf_set_aod(): the face's AOD variant
 *   slot <face> <n> <comp|default> wf_set_slot(), e.g. slot minimal 0 moon
 *   config <cfg>                   wf_config_load() (FACE_CONFIG string); fails on a bad item
 *   config-is <cfg|->              fail unless wf_config_save() gives cfg ("-" = empty)
 *   face-is <id>                   fail unless id is the active face
 *   data demo|clear                demo complication data / everything unknown
 *   lit <max-%>                    fail unless fewer than max-% of the pixels are lit
 *   launcher grid|list             shell_launcher_set_grid(): the LAUNCHER_GRID setting
 *   clock 12|24                    ui_clock_set_24h(): the TIME_24H setting
 *   alarm <HH:MM> [days] [label]   add an alarm; days once|daily|weekdays|weekends (default once)
 *   alarm clear                    remove every alarm
 *   timer <seconds>                start a countdown (runs on the LVGL tick: `wait` moves it)
 *   ring alarm|timer               ring the first enabled alarm / finish the first running timer
 *   world <ids|->                  clock_apps_set_world(): the WORLD_CLOCKS setting ("-" = none)
 *   setting <nvs-key> <value>      set a Settings app value (sim_settings.c); setting-is fails unless it equals value
 *   mode dnd|sleep|theater on|off  turn a mode on/off by hand (sim_modes.c, as quick settings does)
 *   sched dnd|sleep <HH:MM> <HH:MM> [daily|weekdays|weekends]   schedule a mode (default daily)
 *   sched dnd|sleep off            no schedule
 *   battery <pct>                  fake battery level (USB power unchanged)
 *   charger in|out                 plug / unplug USB power (PMU events -> charging screen)
 *   low 15|10|3                    battery at that level and its low-battery flow
 *   watchonly                      the WATCH-ONLY screen for the pinned minute
 *
 * Time only moves with wait/tap/swipe, so runs are deterministic. */
#include "sim_script.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal.h"
#include "hal_sim.h"
#include "alarm_sched.h"
#include "battery_apps.h"
#include "clock_apps.h"
#include "sim_battery.h"
#include "sim_clock.h"
#include "sim_data.h"
#include "sim_modes.h"
#include "sim_settings.h"
#include "shell.h"
#include "sim_screenshot.h"
#include "ui_nav.h"
#include "ui_overlay.h"
#include "ui_root.h"
#include "ui_screens.h"
#include "watch_only.h"
#include "wf_engine.h"
#include "wf_shift.h"

#define STEP_MS 5
/* 2026-10-03 10:09:00 UTC */
#define DEFAULT_NOW ((time_t)1791022140)

static time_t s_now = DEFAULT_NOW;

time_t sim_fixed_now(void)
{
    return s_now;
}

void sim_step(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += STEP_MS) {
        lv_tick_inc(STEP_MS);
        hal_sim_poll();
        lv_timer_handler();
    }
}

static bool cmd_time(const char *arg)
{
    if (strcmp(arg, "unknown") == 0) {
        ui_clock_set_valid(false);
        return true;
    }
    int h = 0;
    int m = 0;
    if (sscanf(arg, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) {
        return false;
    }
    s_now = s_now - s_now % 86400 + h * 3600 + m * 60;
    ui_clock_set_valid(true);
    ui_clock_set_source(sim_fixed_now);
    return true;
}

static bool cmd_slot(const char *arg)
{
    char face[32];
    char comp[32];
    int slot = 0;
    if (sscanf(arg, "%31s %d %31s", face, &slot, comp) != 3 || slot < 0) {
        return false;
    }
    const wf_comp_t c = strcmp(comp, "default") == 0 ? WF_COMP_COUNT : wf_comp_find(comp);
    if (c == WF_COMP_COUNT && strcmp(comp, "default") != 0) {
        fprintf(stderr, "script: unknown complication '%s'\n", comp);
        return false;
    }
    return wf_set_slot(face, (uint8_t)slot, c) == ESP_OK;
}

/* The saved face configuration (what the picker/customize screens would persist). */
static bool cmd_config_is(const char *want)
{
    char cfg[512];
    if (wf_config_save(cfg, sizeof cfg) != ESP_OK) {
        fprintf(stderr, "script: face config does not fit\n");
        return false;
    }
    if (strcmp(want, "-") == 0) {
        want = "";
    }
    printf("face config: '%s' (active %s)\n", cfg, wf_active()->id);
    if (strcmp(cfg, want) != 0) {
        fprintf(stderr, "script: face config is '%s', expected '%s'\n", cfg, want);
        return false;
    }
    return true;
}

/* AOD acceptance (docs/03 F1): fewer than max % of the pixels lit. */
static bool cmd_lit(const char *arg, lv_display_t *disp)
{
    double max = 0;
    if (sscanf(arg, "%lf", &max) != 1) {
        return false;
    }
    const long x100 = sim_lit_pixels_x100(disp);
    printf("lit pixels: %ld.%02ld %% (max %.2f %%)\n", x100 / 100, x100 % 100, max);
    if (x100 >= (long)(max * 100)) {
        fprintf(stderr, "script: %ld.%02ld %% of the pixels are lit, limit %.2f %%\n", x100 / 100, x100 % 100, max);
        return false;
    }
    return true;
}

/* mode dnd|sleep|theater on|off; sched dnd|sleep <HH:MM> <HH:MM> [days] | off */
static bool cmd_mode(bool sched, const char *arg)
{
    char name[16] = "";
    char a1[16] = "";
    char a2[16] = "";
    char a3[16] = "";
    if (sscanf(arg, "%15s %15s %15s %15s", name, a1, a2, a3) < 2) {
        return false;
    }
    int id = 0;
    while (id < MODE_COUNT && strcmp(mode_name((mode_id_t)id), name) != 0) {
        id++;
    }
    if (id == MODE_COUNT) {
        fprintf(stderr, "script: unknown mode '%s'\n", name);
        return false;
    }
    if (!sched) {
        if (strcmp(a1, "on") != 0 && strcmp(a1, "off") != 0) {
            return false;
        }
        sim_modes_set((mode_id_t)id, a1[1] == 'n');
        return true;
    }
    mode_sched_t s = {0};
    if (strcmp(a1, "off") != 0) {
        int h1, m1, h2, m2;
        if (sscanf(a1, "%d:%d", &h1, &m1) != 2 || sscanf(a2, "%d:%d", &h2, &m2) != 2 || h1 < 0 || h1 > 23 ||
            m1 < 0 || m1 > 59 || h2 < 0 || h2 > 23 || m2 < 0 || m2 > 59) {
            return false;
        }
        s.start_min = (uint16_t)(h1 * 60 + m1);
        s.end_min = (uint16_t)(h2 * 60 + m2);
        s.days = !a3[0] || strcmp(a3, "daily") == 0 ? ALARM_DAYS_ALL
                 : strcmp(a3, "weekdays") == 0      ? ALARM_DAYS_WEEKDAYS
                 : strcmp(a3, "weekends") == 0      ? ALARM_DAYS_WEEKEND
                                                    : 0;
        if (!s.days) {
            fprintf(stderr, "script: unknown days '%s'\n", a3);
            return false;
        }
    }
    sim_modes_sched((mode_id_t)id, &s);
    return true;
}

/* The WATCH-ONLY screen as app_main draws it: the minute a tick shows, its burn-in step. */
static void cmd_watch_only(void)
{
    hal_battery_t b = {.percent = -1};
    hal_pmu_read_battery(&b);
    const int64_t minute = watch_only_minute((int64_t)s_now * 1000);
    ui_watch_only_args_t args = {.at = (time_t)(minute * 60), .battery_pct = b.percent};
    wf_aod_shift((uint32_t)minute, &args.dx, &args.dy);
    ui_watch_only_show(&args);
}

static bool cmd_alarm(const char *arg)
{
    if (strcmp(arg, "clear") == 0) {
        sim_clock_clear_alarms();
        return true;
    }
    int h = 0;
    int m = 0;
    int n = 0;
    if (sscanf(arg, "%d:%d %n", &h, &m, &n) < 2 || h < 0 || h > 23 || m < 0 || m > 59) {
        return false;
    }
    const char *rest = arg + n;
    char days_word[16] = "";
    int k = 0;
    uint8_t days = 0;
    if (sscanf(rest, "%15s %n", days_word, &k) >= 1) {
        static const struct {
            const char *name;
            uint8_t days;
        } k_days[] = {{"once", 0}, {"daily", ALARM_DAYS_ALL}, {"weekdays", ALARM_DAYS_WEEKDAYS},
                      {"weekends", ALARM_DAYS_WEEKEND}};
        size_t i = 0;
        while (i < sizeof k_days / sizeof k_days[0] && strcmp(k_days[i].name, days_word) != 0) {
            i++;
        }
        if (i == sizeof k_days / sizeof k_days[0]) {
            fprintf(stderr, "script: unknown repeat '%s'\n", days_word);
            return false;
        }
        days = k_days[i].days;
        rest += k;
    }
    return sim_clock_add_alarm(h, m, days, rest);
}

static bool cmd_swipe(int x1, int y1, int x2, int y2, int ms)
{
    if (ms <= 0) {
        ms = 200;
    }
    const int steps = ms / STEP_MS;
    for (int i = 0; i <= steps; i++) {
        hal_sim_touch_inject(true, (uint16_t)(x1 + (x2 - x1) * i / steps), (uint16_t)(y1 + (y2 - y1) * i / steps));
        sim_step(STEP_MS);
    }
    hal_sim_touch_inject(false, (uint16_t)x2, (uint16_t)y2);
    sim_step(100);
    return true;
}

static bool run_line(char *line, lv_display_t *disp)
{
    char *hash = strchr(line, '#');
    if (hash) {
        *hash = '\0';
    }
    char cmd[16] = "";
    int n = 0;
    if (sscanf(line, " %15s %n", cmd, &n) < 1) {
        return true; /* blank */
    }
    char *arg = line + n;
    arg[strcspn(arg, "\r\n")] = '\0';
    for (size_t len = strlen(arg); len > 0 && (arg[len - 1] == ' ' || arg[len - 1] == '\t'); len--) {
        arg[len - 1] = '\0'; /* before a trailing comment */
    }
    int a = 0, b = 0, c = 0, d = 0, e = 0;

    if (strcmp(cmd, "wait") == 0 && sscanf(arg, "%d", &a) == 1 && a >= 0) {
        sim_step((uint32_t)a);
    } else if (strcmp(cmd, "tap") == 0 && sscanf(arg, "%d %d", &a, &b) == 2) {
        hal_sim_touch_inject(true, (uint16_t)a, (uint16_t)b);
        sim_step(80);
        hal_sim_touch_inject(false, (uint16_t)a, (uint16_t)b);
        sim_step(100);
    } else if (strcmp(cmd, "hold") == 0 && sscanf(arg, "%d %d %d", &a, &b, &c) >= 2) {
        hal_sim_touch_inject(true, (uint16_t)a, (uint16_t)b);
        sim_step(c > 0 ? (uint32_t)c : 800);
        hal_sim_touch_inject(false, (uint16_t)a, (uint16_t)b);
        sim_step(100);
    } else if (strcmp(cmd, "swipe") == 0 && sscanf(arg, "%d %d %d %d %d", &a, &b, &c, &d, &e) >= 4) {
        cmd_swipe(a, b, c, d, e);
    } else if (strcmp(cmd, "key") == 0 && (strcmp(arg, "back") == 0 || strcmp(arg, "power") == 0)) {
        const hal_button_t btn = strcmp(arg, "back") == 0 ? HAL_BUTTON_BACK : HAL_BUTTON_POWER;
        hal_sim_button_inject(btn, true);
        sim_step(50);
        hal_sim_button_inject(btn, false);
        sim_step(50);
    } else if (strcmp(cmd, "push") == 0 && *arg) {
        if (ui_nav_push_id(arg, NULL) != ESP_OK) {
            fprintf(stderr, "script: cannot push '%s'\n", arg);
            return false;
        }
    } else if (strcmp(cmd, "back") == 0) {
        ui_nav_back();
    } else if (strcmp(cmd, "home") == 0) {
        ui_nav_home();
    } else if (strcmp(cmd, "clobber") == 0) {
        lv_screen_load_anim(lv_obj_create(NULL), LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    } else if (strcmp(cmd, "start") == 0) {
        if (ui_start() != ESP_OK) {
            fprintf(stderr, "script: ui_start failed\n");
            return false;
        }
    } else if (strcmp(cmd, "toast") == 0) {
        ui_toast_show(arg, 0);
    } else if (strcmp(cmd, "time") == 0 && cmd_time(arg)) {
        /* done */
    } else if (strcmp(cmd, "face") == 0 && *arg) {
        if (wf_set_active(arg) != ESP_OK) {
            fprintf(stderr, "script: unknown face '%s'\n", arg);
            return false;
        }
    } else if (strcmp(cmd, "aod") == 0 && (strcmp(arg, "on") == 0 || strcmp(arg, "off") == 0)) {
        wf_set_aod(strcmp(arg, "on") == 0);
    } else if (strcmp(cmd, "slot") == 0 && cmd_slot(arg)) {
        /* done */
    } else if (strcmp(cmd, "config") == 0) {
        const int bad = wf_config_load(arg);
        if (bad != 0) {
            fprintf(stderr, "script: %d bad face config item(s) in '%s'\n", bad, arg);
            return false;
        }
    } else if (strcmp(cmd, "config-is") == 0 && *arg) {
        return cmd_config_is(arg);
    } else if (strcmp(cmd, "face-is") == 0 && *arg) {
        if (strcmp(wf_active()->id, arg) != 0) {
            fprintf(stderr, "script: active face is '%s', expected '%s'\n", wf_active()->id, arg);
            return false;
        }
    } else if (strcmp(cmd, "data") == 0 && (strcmp(arg, "demo") == 0 || strcmp(arg, "clear") == 0)) {
        if (arg[0] == 'd') {
            sim_data_demo();
        } else {
            sim_data_clear();
        }
    } else if (strcmp(cmd, "launcher") == 0 && (strcmp(arg, "grid") == 0 || strcmp(arg, "list") == 0)) {
        shell_launcher_set_grid(arg[0] == 'g');
    } else if (strcmp(cmd, "clock") == 0 && (strcmp(arg, "12") == 0 || strcmp(arg, "24") == 0)) {
        ui_clock_set_24h(arg[0] == '2');
    } else if (strcmp(cmd, "alarm") == 0 && *arg) {
        return cmd_alarm(arg);
    } else if (strcmp(cmd, "timer") == 0 && sscanf(arg, "%d", &a) == 1 && a > 0) {
        return sim_clock_start_timer((uint32_t)a * 1000u);
    } else if (strcmp(cmd, "ring") == 0 && (strcmp(arg, "alarm") == 0 || strcmp(arg, "timer") == 0)) {
        if (!sim_clock_ring(arg[0] == 'a')) {
            fprintf(stderr, "script: nothing to ring\n");
            return false;
        }
    } else if (strcmp(cmd, "world") == 0 && *arg) {
        clock_apps_set_world(strcmp(arg, "-") == 0 ? "" : arg);
    } else if (strcmp(cmd, "setting") == 0 || strcmp(cmd, "setting-is") == 0) {
        char key[32];
        char value[64];
        if (sscanf(arg, "%31s %63[^\n]", key, value) != 2) {
            fprintf(stderr, "script: usage: %s <nvs-key> <value>\n", cmd);
            return false;
        }
        if (cmd[7] == '\0') {
            if (!sim_settings_set(key, value)) {
                fprintf(stderr, "script: cannot set %s to '%s'\n", key, value);
                return false;
            }
        } else if (!sim_settings_is(key, value)) {
            fprintf(stderr, "script: setting %s is not '%s'\n", key, value);
            return false;
        }
    } else if (strcmp(cmd, "mode") == 0 || strcmp(cmd, "sched") == 0) {
        return cmd_mode(cmd[0] == 's', arg);
    } else if (strcmp(cmd, "battery") == 0 && sscanf(arg, "%d", &a) == 1 && a >= 0 && a <= 100) {
        hal_battery_t bat;
        hal_pmu_read_battery(&bat);
        sim_battery_set(a, bat.vbus);
    } else if (strcmp(cmd, "charger") == 0 && (strcmp(arg, "in") == 0 || strcmp(arg, "out") == 0)) {
        hal_battery_t bat;
        hal_pmu_read_battery(&bat);
        sim_battery_set(bat.percent, arg[0] == 'i');
    } else if (strcmp(cmd, "low") == 0 && sscanf(arg, "%d", &a) == 1 && (a == 15 || a == 10 || a == 3)) {
        sim_battery_set(a, false);
        battery_apps_low((uint8_t)a, (int8_t)a);
    } else if (strcmp(cmd, "watchonly") == 0) {
        cmd_watch_only();
    } else if (strcmp(cmd, "lit") == 0) {
        return cmd_lit(arg, disp);
    } else if (strcmp(cmd, "shot") == 0 && *arg) {
        return sim_screenshot_save(disp, arg);
    } else {
        return false;
    }
    return true;
}

bool sim_script_run(const char *path, lv_display_t *disp)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "script: cannot open %s\n", path);
        return false;
    }
    char line[256];
    int no = 0;
    bool ok = true;
    while (ok && fgets(line, sizeof line, f)) {
        no++;
        char copy[256];
        memcpy(copy, line, sizeof copy);
        ok = run_line(line, disp);
        if (!ok) {
            fprintf(stderr, "%s:%d: bad command: %s", path, no, copy);
        }
    }
    fclose(f);
    return ok;
}
