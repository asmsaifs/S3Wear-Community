// Console: `modes [dnd|sleep|theater on|off]` and `modes sched dnd|sleep off|HH:MM HH:MM [days]` —
// svc_modes state and schedules (P3-08).
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "alarm_sched.h"
#include "esp_check.h"
#include "esp_console.h"
#include "svc_diag_priv.h"
#include "svc_modes.h"

static const char *TAG = "cmd_modes";

static bool parse_mode(const char *s, mode_id_t *out)
{
    for (int i = 0; i < MODE_COUNT; i++) {
        if (strcmp(s, mode_name((mode_id_t)i)) == 0) {
            *out = (mode_id_t)i;
            return true;
        }
    }
    return false;
}

static bool parse_hhmm(const char *s, uint16_t *min)
{
    int h;
    int m;
    if (sscanf(s, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) {
        return false;
    }
    *min = (uint16_t)(h * 60 + m);
    return true;
}

static bool parse_days(const char *s, uint8_t *days)
{
    if (strcmp(s, "daily") == 0) {
        *days = ALARM_DAYS_ALL;
    } else if (strcmp(s, "weekdays") == 0) {
        *days = ALARM_DAYS_WEEKDAYS;
    } else if (strcmp(s, "weekends") == 0) {
        *days = ALARM_DAYS_WEEKEND;
    } else {
        // Weekday digits, 0 = Sunday .. 6 = Saturday: the days a window starts.
        uint8_t d = 0;
        for (const char *p = s; *p; p++) {
            if (*p < '0' || *p > '6') {
                return false;
            }
            d |= (uint8_t)(1u << (*p - '0'));
        }
        *days = d;
    }
    return *days != 0;
}

static int status(void)
{
    svc_modes_status_t st;
    svc_modes_get_status(&st);
    printf("DND %s, sleep %s, theater %s -> quiet %d, dark %d (%lu changes)\n", st.state.dnd ? "on" : "off",
           st.state.sleep ? "on" : "off", st.state.theater ? "on" : "off", st.state.quiet, st.state.dark,
           (unsigned long)st.changes);
    for (int i = 0; i < MODE_COUNT; i++) {
        const mode_sched_t *s = &st.m.sched[i];
        printf("%-8s by hand %d%s", mode_name((mode_id_t)i), st.m.manual[i], st.m.skip[i] ? ", window skipped" : "");
        if (i != MODE_THEATER) {
            if (s->days) {
                char days[32];
                alarm_days_text(s->days, days, sizeof days);
                printf(", schedule %02u:%02u-%02u:%02u %s", s->start_min / 60, s->start_min % 60, s->end_min / 60,
                       s->end_min % 60, days);
            } else {
                printf(", no schedule");
            }
        }
        printf("\n");
    }
    if (st.next_edge > 0) {
        const time_t t = (time_t)st.next_edge;
        struct tm lt;
        localtime_r(&t, &lt);
        char buf[32];
        strftime(buf, sizeof buf, "%a %H:%M:%S", &lt);
        printf("next schedule edge: %s local (in %lld s)\n", buf, (long long)(st.next_edge - time(NULL)));
    }
    return 0;
}

static int sched(int argc, char **argv)
{
    mode_id_t id;
    if (argc < 4 || !parse_mode(argv[2], &id) || id == MODE_THEATER) {
        return -1;
    }
    mode_sched_t s;
    svc_modes_get_sched(id, &s);
    if (strcmp(argv[3], "off") == 0) {
        s.days = 0;
    } else if (argc >= 5 && parse_hhmm(argv[3], &s.start_min) && parse_hhmm(argv[4], &s.end_min)) {
        s.days = ALARM_DAYS_ALL;
        if (argc >= 6 && !parse_days(argv[5], &s.days)) {
            printf("days: daily|weekdays|weekends|<digits 0-6>\n");
            return 1;
        }
    } else {
        return -1;
    }
    const esp_err_t err = svc_modes_set_sched(id, &s);
    if (err != ESP_OK) {
        printf("failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    return 0;
}

static int cmd_modes(int argc, char **argv)
{
    if (argc < 2) {
        return status();
    }
    int r = -1;
    mode_id_t id;
    if (strcmp(argv[1], "sched") == 0) {
        r = sched(argc, argv);
    } else if (argc >= 3 && parse_mode(argv[1], &id) &&
               (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0)) {
        r = svc_modes_set(id, strcmp(argv[2], "on") == 0) == ESP_OK ? 0 : 1;
    }
    if (r < 0) {
        printf("usage: modes | modes dnd|sleep|theater on|off | modes sched dnd|sleep off|HH:MM HH:MM [days]\n");
        return 1;
    }
    return r;
}

esp_err_t diag_register_modes(void)
{
    const esp_console_cmd_t cmd = {
        .command = "modes",
        .help = "svc_modes: DND, sleep and theater mode state and schedules; <mode> on|off by hand; "
                "sched dnd|sleep HH:MM HH:MM [daily|weekdays|weekends|<digits 0=Sun..6>] (start days), "
                "sched dnd|sleep off",
        .hint = "[dnd|sleep|theater on|off|sched dnd|sleep off|HH:MM HH:MM [days]]",
        .func = cmd_modes,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&cmd), TAG, "modes");
    return ESP_OK;
}
