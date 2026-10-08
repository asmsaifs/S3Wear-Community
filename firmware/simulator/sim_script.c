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
 *   pro locked|unlocked            shell_set_pro_locked(): no valid Pro licence (svc_license, Pro only)
 *   clock 12|24                    ui_clock_set_24h(): the TIME_24H setting
 *   alarm <HH:MM> [days] [label]   add an alarm; days once|daily|weekdays|weekends (default once)
 *   alarm clear                    remove every alarm
 *   timer <seconds>                start a countdown (runs on the LVGL tick: `wait` moves it)
 *   ring alarm|timer               ring the first enabled alarm / finish the first running timer
 *   world <ids|->                  clock_apps_set_world(): the WORLD_CLOCKS setting ("-" = none)
 *   app <name|id|file.wasm>        run a mini app (sim_app.c): installed id, widgets (SDK sample) or a test app
 *   app-install <pkg.s3app>        check a package, show the consent screen (P8-05)
 *   app-uninstall <id>             uninstall at once
 *   app-installed <id> <ver|none>  fail unless that version (or none) is installed
 *   app-stop                       the system stops the app (s3w_on_stop)
 *   app-is running|exited|trapped|hung|none   fail unless the app's state is that
 *   setting <nvs-key> <value>      set a Settings app value (sim_settings.c); setting-is fails unless it equals value
 *   mode dnd|sleep|theater on|off  turn a mode on/off by hand (sim_modes.c, as quick settings does)
 *   sched dnd|sleep <HH:MM> <HH:MM> [daily|weekdays|weekends]   schedule a mode (default daily)
 *   sched dnd|sleep off            no schedule
 *   battery <pct>                  fake battery level (USB power unchanged)
 *   charger in|out                 plug / unplug USB power (PMU events -> charging screen)
 *   low 15|10|3                    battery at that level and its low-battery flow
 *   watchonly                      the WATCH-ONLY screen for the pinned minute
 *   pair <code> [replace]          a phone asks to pair (svc_ble's request): the code alert
 *   pair-end ok|fail               the pairing ended (phone_apps_pair_done)
 *   pair-is yes|no|none            fail unless the user's answer to the last request is that
 *   find-watch on|off              the phone starts / stops the watch ringing (sim_find.c)
 *   find-phone <state>             the phone's answer: idle|ringing|stopped|offline|noanswer|unsupported|refused
 *   find-asked ring|stop|none      fail unless the app's last request to the phone was that
 *   find-stopped yes|no            fail unless the user stopped the watch ringing (or not)
 *   flash-boost yes|no        fail unless the panel is at full brightness for the flashlight (or not)
 *   flash-keeps-on yes|no     fail unless the screen is held on by the top screen (or not)
 *   media none|offline|playing|paused|long|nonlatin|live|novol   the phone's media session (sim_media.c)
 *   media-art demo|none|broken     the artwork arrives: the demo JPEG (decoded), none, or one that fails to decode
 *   media-text on|off              the phone draws titles the fonts lack (bars stand in for the text)
 *   media-asked <cmd>|none         fail unless the Media app's last command was that (play, pause, next, prev, vol+, vol-)
 *   media-fail offline|noanswer|unsupported|refused   a media command failed: the toast
 *   weather demo|stale|expired|current|night|none   the phone's forecast (sim_weather.c)
 *   calendar demo|empty|none       the phone's agenda (sim_calendar.c)
 *   call ring|ring-nocontrol <name>|<number>   an incoming call (sim_call.c)
 *   call answered|end|missed       the phone answered / ended / missed the call
 *   call active <name>|<number>    a call dialled on the phone
 *   call missed-demo               three missed calls (today, yesterday, last week)
 *   call-asked <cmd>|clear|silenced|none   fail unless the screens' last request was that
 *   call-fail offline|noanswer|unsupported|denied|refused   the last call command failed: the toast
 *   link connected|reconnecting|unpaired   the phone link state on Settings > Connections (sim_connect.c)
 *   link-sync <seconds>|never      age of the last sync with the phone (default 300)
 *   link-bt-is on|off              fail unless the Bluetooth switch is that
 *   link-asked reconnect|forget|none   fail unless the Connections page's last request was that
 *   wifi off|searching|joining|joined|notfound|auth|noip   Wi-Fi state on Settings > Connections > Wi-Fi
 *   wifi-saved <ssid>[,<ssid>...]|none   the saved Wi-Fi networks (default "Home")
 *   wifi-is on|off                 fail unless the Wi-Fi switch is that
 *   wifi-forgot <ssid>|none        fail unless the last network forgotten on the Wi-Fi page was that
 *   ha demo|unread|empty|none      Home Assistant entities and their states (sim_ha.c)
 *   ha-via wifi|phone, ha-error <err>|none, ha-busy <n>, ha-refreshing   how the last call went
 *   ha-asked tap <n>|refresh|none  fail unless the Home screens' last request was that
 *   memo demo|none                 voice memos: three (the newest being sent, one on the phone) / none (sim_memo.c)
 *   memo-state idle|rec <s> <level>|play <n> <s>   the recorder / player state
 *   memo-result <how>              saved|full|short|nospace|mic|storage|played|playfail: how it ended
 *   memo-asked <what>|none         fail unless the app's last request was that (record, stop, play <n>, delete <n>)
 *
 * The Community build (S3W_EDITION_PRO off) has no notif, find, flash, media, weather, calendar,
 * call, ha or memo commands (docs/10 §3).
 *
 * Time only moves with wait/tap/swipe, so runs are deterministic. */
#include "sim_script.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal.h"
#include "hal_sim.h"
#include "phone_apps.h"
#include "alarm_sched.h"
#include "battery_apps.h"
#include "clock_apps.h"
#if S3W_EDITION_PRO
#include "sim_app.h"
#endif
#include "sim_battery.h"
#include "sim_clock.h"
#include "sim_data.h"
#include "sim_modes.h"
#include "s3w_edition.h"
#include "sim_connect.h"
#if S3W_EDITION_PRO
#include "sim_notify.h"
#include "sim_find.h"
#include "sim_flashlight.h"
#include "sim_media.h"
#include "sim_weather.h"
#include "sim_calendar.h"
#include "sim_call.h"
#include "sim_ha.h"
#include "sim_memo.h"
#endif
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

// Pairing: the answer the user gave to the last request (svc_ble_pair_reply on the watch).
static const char *s_pair_answer = "none";

static void pair_reply(bool accept, void *ctx)
{
    (void)ctx;
    s_pair_answer = accept ? "yes" : "no";
}

static bool cmd_pair(const char *cmd, const char *arg)
{
    if (strcmp(cmd, "pair-end") == 0 && (strcmp(arg, "ok") == 0 || strcmp(arg, "fail") == 0)) {
        phone_apps_pair_done(arg[0] == 'o');
        return true;
    }
    if (strcmp(cmd, "pair-is") == 0) {
        if (strcmp(arg, s_pair_answer) != 0) {
            fprintf(stderr, "script: pairing answer is %s, not %s\n", s_pair_answer, arg);
            return false;
        }
        return true;
    }
    unsigned long code;
    char extra[16] = "";
    if (sscanf(arg, "%lu %15s", &code, extra) < 1 || code > 999999 || (extra[0] && strcmp(extra, "replace") != 0)) {
        fprintf(stderr, "script: usage: pair <code> [replace]\n");
        return false;
    }
    s_pair_answer = "none";
    phone_apps_pair_request((uint32_t)code, extra[0] != '\0', pair_reply, NULL);
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
    } else if (strcmp(cmd, "pro") == 0 && S3W_EDITION_PRO &&
               (strcmp(arg, "locked") == 0 || strcmp(arg, "unlocked") == 0)) {
        shell_set_pro_locked(arg[0] == 'l');
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
#if S3W_EDITION_PRO
    } else if (strncmp(cmd, "notif", 5) == 0) {
        return sim_notify_cmd(cmd, arg);
    } else if (strncmp(cmd, "find-", 5) == 0) {
        return sim_find_cmd(cmd, arg);
    } else if (strncmp(cmd, "flash-", 6) == 0) {
        return sim_flashlight_cmd(cmd, arg);
    } else if (strncmp(cmd, "media", 5) == 0) {
        return sim_media_cmd(cmd, arg);
    } else if (strcmp(cmd, "weather") == 0) {
        return sim_weather_cmd(cmd, arg);
    } else if (strcmp(cmd, "calendar") == 0) {
        return sim_calendar_cmd(cmd, arg);
    } else if (strcmp(cmd, "call") == 0 || strncmp(cmd, "call-", 5) == 0) {
        return sim_call_cmd(cmd, arg);
    } else if (strcmp(cmd, "ha") == 0 || strncmp(cmd, "ha-", 3) == 0) {
        return sim_ha_cmd(cmd, arg);
    } else if (strcmp(cmd, "memo") == 0 || strncmp(cmd, "memo-", 5) == 0) {
        return sim_memo_cmd(cmd, arg);
#endif
    } else if (strncmp(cmd, "link", 4) == 0 || strncmp(cmd, "wifi", 4) == 0) {
        return sim_connect_cmd(cmd, arg);
#if S3W_EDITION_PRO
    } else if (strcmp(cmd, "app") == 0 || strncmp(cmd, "app-", 4) == 0 || strcmp(cmd, "imu") == 0) {
        return sim_app_cmd(cmd, arg);
#endif
    } else if (strncmp(cmd, "pair", 4) == 0) {
        return cmd_pair(cmd, arg);
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
