// Console: `alarm [add|in|del|on|off|snooze|stop]` and `timer [<s>|stop <id>]` —
// svc_alarm alarms, countdowns and ringing (P3-07).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_check.h"
#include "esp_console.h"
#include "svc_alarm.h"
#include "svc_diag_priv.h"
#include "svc_sensors.h"

static const char *TAG = "cmd_alarm";

static void print_time(const char *what, int64_t utc)
{
    if (utc <= 0) {
        printf("%s: none\n", what);
        return;
    }
    const time_t t = (time_t)utc;
    struct tm lt;
    localtime_r(&t, &lt);
    char buf[32];
    strftime(buf, sizeof buf, "%a %Y-%m-%d %H:%M:%S", &lt);
    printf("%s: %s local (in %lld s)\n", what, buf, (long long)(utc - time(NULL)));
}

static int alarm_status(void)
{
    static alarm_set_t set; // console task only
    svc_alarm_get(&set);
    for (int i = 0; i < set.count; i++) {
        const alarm_t *a = &set.items[i];
        char days[32];
        alarm_days_text(a->days, days, sizeof days);
        printf("%3u  %02u:%02u  %-3s  %-14s snooze %2u min  %s\n", a->id, a->hour, a->minute, a->enabled ? "on" : "off",
               days, a->snooze_min, a->label);
    }
    if (set.count == 0) {
        printf("no alarms\n");
    }
    if (set.snooze_id) {
        printf("snoozed: alarm %u\n", set.snooze_id);
    }
    print_time("next", svc_alarm_next());

    static timer_set_t timers; // console task only
    uint32_t now = 0;
    svc_alarm_timers(&timers, &now);
    static const char *const k_state[] = {"running", "paused", "done"};
    for (int i = 0; i < timers.count; i++) {
        const countdown_t *c = &timers.items[i];
        printf("timer %u: %s, %lu of %lu s left\n", c->id, k_state[c->state % 3],
               (unsigned long)(timer_left_ms(c, now) / 1000), (unsigned long)(c->duration_ms / 1000));
    }

    svc_alarm_evt_ring_t r;
    if (svc_alarm_ringing(&r)) {
        printf("ringing: %s %u\n", r.kind == SVC_ALARM_RING_ALARM ? "alarm" : "timer", r.id);
    }
    svc_alarm_stats_t st;
    svc_alarm_get_stats(&st);
    svc_sensors_stats_t ss;
    svc_sensors_get_stats(&ss);
    printf("rings %lu (last %+ld ms from its time), RTC alarm %s, RTC IRQs %lu, speaker %s, flips %lu\n",
           (unsigned long)st.rings, (long)st.last_late_ms, st.rtc_armed ? "armed" : "off", (unsigned long)st.rtc_irqs,
           st.audio ? "yes" : "no", (unsigned long)ss.flips);
    printf("svc_alarm stack: %lu B never used\n", (unsigned long)st.stack_free);
    return 0;
}

static bool parse_days(const char *s, uint8_t *days)
{
    if (strcmp(s, "once") == 0) {
        *days = 0;
    } else if (strcmp(s, "daily") == 0) {
        *days = ALARM_DAYS_ALL;
    } else if (strcmp(s, "weekdays") == 0) {
        *days = ALARM_DAYS_WEEKDAYS;
    } else if (strcmp(s, "weekends") == 0) {
        *days = ALARM_DAYS_WEEKEND;
    } else {
        // Weekday digits, 0 = Sunday .. 6 = Saturday ("135" = Mon Wed Fri).
        uint8_t d = 0;
        for (const char *p = s; *p; p++) {
            if (*p < '0' || *p > '6') {
                return false;
            }
            d |= (uint8_t)(1u << (*p - '0'));
        }
        *days = d;
    }
    return true;
}

static int put(alarm_t *a, int argc, char **argv, int first)
{
    if (argc > first && !parse_days(argv[first], &a->days)) {
        printf("repeat: once|daily|weekdays|weekends|<digits 0-6>\n");
        return 1;
    }
    if (argc > first + 1) {
        snprintf(a->label, sizeof a->label, "%s", argv[first + 1]);
    }
    uint8_t id = 0;
    const esp_err_t err = svc_alarm_put(a, &id);
    if (err != ESP_OK) {
        printf("failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("alarm %u: %02u:%02u\n", id, a->hour, a->minute);
    return 0;
}

static int set_enabled(uint8_t id, bool on)
{
    static alarm_set_t set; // console task only
    svc_alarm_get(&set);
    alarm_t *a = alarm_set_find(&set, id);
    if (a == NULL) {
        printf("no alarm %u\n", id);
        return 1;
    }
    a->enabled = on;
    return svc_alarm_put(a, NULL) == ESP_OK ? 0 : 1;
}

static int cmd_alarm(int argc, char **argv)
{
    if (argc < 2) {
        return alarm_status();
    }
    const char *sub = argv[1];
    alarm_t a;
    int h = 0;
    int m = 0;
    if (strcmp(sub, "add") == 0 && argc >= 3 && sscanf(argv[2], "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 &&
        m >= 0 && m < 60) {
        alarm_default(&a, (uint8_t)h, (uint8_t)m);
        return put(&a, argc, argv, 3);
    }
    if (strcmp(sub, "in") == 0 && argc >= 3 && atoi(argv[2]) > 0) {
        // One-time alarm at the start of the minute that is n minutes from now.
        const time_t t = time(NULL) + (time_t)atoi(argv[2]) * 60;
        struct tm lt;
        localtime_r(&t, &lt);
        alarm_default(&a, (uint8_t)lt.tm_hour, (uint8_t)lt.tm_min);
        snprintf(a.label, sizeof a.label, "Test");
        return put(&a, argc, argv, 3);
    }
    if (strcmp(sub, "del") == 0 && argc >= 3) {
        return svc_alarm_delete((uint8_t)atoi(argv[2])) == ESP_OK ? 0 : 1;
    }
    if ((strcmp(sub, "on") == 0 || strcmp(sub, "off") == 0) && argc >= 3) {
        return set_enabled((uint8_t)atoi(argv[2]), strcmp(sub, "on") == 0);
    }
    if (strcmp(sub, "snooze") == 0) {
        return svc_alarm_snooze() == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "stop") == 0) {
        return svc_alarm_dismiss() == ESP_OK ? 0 : 1;
    }
    printf("usage: alarm | alarm add HH:MM [repeat] [label] | alarm in <min> | alarm del|on|off <id> | "
           "alarm snooze|stop\n");
    return 1;
}

static int cmd_timer(int argc, char **argv)
{
    if (argc < 2) {
        return alarm_status();
    }
    if (strcmp(argv[1], "stop") == 0 && argc >= 3) {
        return svc_alarm_timer_action((uint8_t)atoi(argv[2]), SVC_TIMER_REMOVE) == ESP_OK ? 0 : 1;
    }
    const int s = atoi(argv[1]);
    if (s <= 0) {
        printf("usage: timer <seconds> | timer stop <id>\n");
        return 1;
    }
    uint8_t id = 0;
    const esp_err_t err = svc_alarm_timer_start((uint32_t)s * 1000u, &id);
    printf(err == ESP_OK ? "timer %u: %d s\n" : "failed\n", id, s);
    return err == ESP_OK ? 0 : 1;
}

esp_err_t diag_register_alarm(void)
{
    const esp_console_cmd_t alarm = {
        .command = "alarm",
        .help = "svc_alarm: list alarms, snooze, next alarm, timers, ring stats; add HH:MM [once|daily|weekdays|"
                "weekends|<digits 0=Sun..6>] [label]; in <min> = one-time test alarm; del|on|off <id>; "
                "snooze|stop the ringing one",
        .hint = "[add HH:MM [repeat] [label]|in <min>|del <id>|on <id>|off <id>|snooze|stop]",
        .func = cmd_alarm,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&alarm), TAG, "alarm");
    const esp_console_cmd_t timer = {
        .command = "timer",
        .help = "svc_alarm countdowns: <seconds> starts one, stop <id> removes one; no argument lists them",
        .hint = "[<seconds>|stop <id>]",
        .func = cmd_timer,
    };
    return esp_console_cmd_register(&timer);
}
