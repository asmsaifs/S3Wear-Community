// Console: `power` (svc_power state, battery, stats and requests) and `pm stats`
// (esp_pm lock and mode times, light sleep) — P2-06.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_console.h"
#include "esp_pm.h"
#include "sdkconfig.h"
#include "svc_diag_priv.h"
#include "svc_power.h"
#include "svc_settings.h"

static void print_light_sleep(const svc_power_stats_t *st)
{
#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
    uint64_t up_ms = 0;
    for (int i = 0; i < POWER_STATE_COUNT; i++) {
        up_ms += st->state_ms[i];
    }
    printf("light sleep: %lu entries, %llu.%03llu s (%llu%% of uptime)\n", (unsigned long)st->light_sleeps,
           st->light_sleep_us / 1000000, (st->light_sleep_us / 1000) % 1000,
           up_ms ? st->light_sleep_us / 10 / up_ms : 0);
#else
    (void)st;
    printf("light sleep: counters off (CONFIG_PM_LIGHT_SLEEP_CALLBACKS)\n");
#endif
}

static int power_status(void)
{
    svc_power_stats_t st;
    svc_power_get_stats(&st);
    printf("state %s%s, timeout %lu s, holds %u, wake on tap %d, AOD %d\n", power_state_name(st.state),
           st.saver ? " (saver)" : "", (unsigned long)(st.timeout_ms / 1000), st.holds,
           svc_settings_get_bool(S3W_SETTING_WAKE_ON_TAP), svc_settings_get_bool(S3W_SETTING_AOD));
    static svc_power_battery_info_t info; // console task: keep the history off its stack
    if (svc_power_battery_info(&info) == ESP_OK) {
        const svc_power_battery_t *b = &info.battery;
        printf("battery %d%% %u mV%s%s", b->percent, b->mv, b->charging ? ", charging" : "", b->vbus ? ", USB" : "");
        if (info.minutes >= 0) {
            printf(", %s ~%ld min", b->charging ? "full in" : "left", (long)info.minutes);
        }
        printf("\n");
    } else {
        printf("battery: not read yet\n");
    }
    printf("time in state:");
    for (int i = 0; i < POWER_STATE_COUNT; i++) {
        if (st.state_ms[i]) {
            printf(" %s %llu s", power_state_name((power_state_t)i), st.state_ms[i] / 1000);
        }
    }
    printf("\nscreen wakes:");
    for (int i = 0; i < SVC_POWER_WAKE_COUNT; i++) {
        if (st.wakes[i]) {
            printf(" %s %lu", svc_power_wake_name((svc_power_wake_t)i), (unsigned long)st.wakes[i]);
        }
    }
    printf("\n");
    if (st.wakes[SVC_POWER_WAKE_RAISE]) {
        printf("raise wakes untouched before screen off: %lu\n", (unsigned long)st.raise_unused);
    }
    print_light_sleep(&st);
    if (st.seg_start_pct >= 0) {
        printf("drain: on battery from %d%% for %lu s", st.seg_start_pct, (unsigned long)(st.seg_ms / 1000));
    } else {
        printf("drain: on USB power");
    }
    if (st.last_drain_ma_x10) {
        printf(", last 1%% step ~%lu.%lu mA", (unsigned long)(st.last_drain_ma_x10 / 10),
               (unsigned long)(st.last_drain_ma_x10 % 10));
    }
    printf("\n");
    printf("svc_power stack: %lu B never used\n", (unsigned long)st.stack_free);
    return 0;
}

static int check(esp_err_t err)
{
    if (err != ESP_OK) {
        printf("failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    return 0;
}

static int cmd_power(int argc, char **argv)
{
    const char *sub = argc >= 2 ? argv[1] : "";
    const char *arg = argc >= 3 ? argv[2] : "";
    if (argc < 2) {
        return power_status();
    }
    if (strcmp(sub, "screen") == 0 && (strcmp(arg, "on") == 0 || strcmp(arg, "off") == 0)) {
        return check(arg[1] == 'n' ? svc_power_wake(SVC_POWER_WAKE_CONSOLE) : svc_power_screen_off());
    }
    if (strcmp(sub, "saver") == 0 && (strcmp(arg, "on") == 0 || strcmp(arg, "off") == 0)) {
        return check(svc_power_set_saver(arg[1] == 'n'));
    }
    if (strcmp(sub, "timeout") == 0 && argc == 3) {
        return check(svc_settings_set_int(S3W_SETTING_SCREEN_TIMEOUT_S, atoi(arg)));
    }
    if (strcmp(sub, "hold") == 0 && (strcmp(arg, "on") == 0 || strcmp(arg, "off") == 0)) {
        return check(svc_power_hold_screen(arg[1] == 'n'));
    }
    if (strcmp(sub, "watchonly") == 0) {
        return check(svc_power_enter_watch_only());
    }
    if (strcmp(sub, "shutdown") == 0) {
        return check(svc_power_shutdown());
    }
    if (strcmp(sub, "restart") == 0) {
        return check(svc_power_restart());
    }
    printf("usage: power [screen on|off | saver on|off | timeout <s> | hold on|off | watchonly | shutdown | "
           "restart]\n");
    return 1;
}

static int cmd_pm(int argc, char **argv)
{
    if (argc != 2 || strcmp(argv[1], "stats") != 0) {
        printf("usage: pm stats\n");
        return 1;
    }
#if CONFIG_PM_PROFILING
    esp_pm_dump_locks(stdout);
#else
    printf("lock/mode times need CONFIG_PM_PROFILING (dev builds)\n");
#endif
    svc_power_stats_t st;
    svc_power_get_stats(&st);
    print_light_sleep(&st);
    return 0;
}

esp_err_t diag_register_power(void)
{
    const esp_console_cmd_t power = {
        .command = "power",
        .help = "svc_power: no args = state, battery, time per state, wakes, light sleep, drain estimate. "
                "screen on|off, saver on|off, timeout <s>, hold on|off (keep screen on), watchonly, shutdown, restart",
        .hint = "[screen|saver|timeout|hold|watchonly|shutdown|restart]",
        .func = cmd_power,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&power), "cmd_power", "power");
    const esp_console_cmd_t pm = {
        .command = "pm",
        .help = "pm stats: esp_pm locks and time per mode (CPU max, APB max/min, light sleep) since boot",
        .hint = "stats",
        .func = cmd_pm,
    };
    return esp_console_cmd_register(&pm);
}
