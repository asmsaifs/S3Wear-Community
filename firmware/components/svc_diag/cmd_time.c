// Console: `time` — svc_time state, set/sync, zone, 12/24 h, drift calibration (P3-01).
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "esp_console.h"
#include "svc_diag_priv.h"
#include "svc_time.h"
#include "tz_posix.h"

static void print_utc(const char *label, time_t t)
{
    struct tm tm;
    gmtime_r(&t, &tm);
    printf("%s%04d-%02d-%02d %02d:%02d:%02d UTC\n", label, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
           tm.tm_min, tm.tm_sec);
}

static int time_show(void)
{
    svc_time_status_t st;
    svc_time_get_status(&st);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    printf("valid %s, source %s, %s h\n", st.valid ? "yes" : "NO (time unknown)", svc_time_source_name(st.source),
           svc_time_is_24h() ? "24" : "12");
    print_utc("utc      ", tv.tv_sec);
    struct tm lt;
    localtime_r(&tv.tv_sec, &lt); // newlib, with the fixed offset svc_time gave it
    char off[16];
    tz_posix_format_offset(st.offset_s, off, sizeof off);
    printf("local    %04d-%02d-%02d %02d:%02d:%02d %s (%s%s)\n", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
           lt.tm_hour, lt.tm_min, lt.tm_sec, st.abbr, off, st.dst ? ", DST" : "");
    printf("zone     %s  (newlib TZ=%s)\n", st.tz, st.newlib_tz);
    if (st.next_transition != INT64_MAX) {
        print_utc("next DST ", (time_t)st.next_transition);
    }
    const time_drift_t *d = &st.drift;
    printf("drift    offset %d steps (%+d ppb), last measured %+" PRId32 " ppb\n", d->steps,
           d->steps * TIME_DRIFT_STEP_PPB, d->last_ppb);
    if (d->anchor_ms > 0) {
        const int64_t span_s = (int64_t)tv.tv_sec - d->anchor_ms / 1000;
        printf("         window %" PRId64 " h of %d h, committed error %+" PRId64 " ms, RTC bias %+" PRId32 " ms\n",
               span_s / 3600, TIME_DRIFT_MIN_SPAN_S / 3600, d->err_ms, d->rtc_bias_ms);
    } else {
        printf("         no window (starts at the next phone/SNTP sync)\n");
    }
    if (st.rtc_write_pending) {
        printf("RTC write pending (next whole second)\n");
    }
    return 0;
}

// "YYYY-MM-DD HH:MM:SS" (UTC) at argv[i], or Unix seconds/ms; -1 if neither.
static int64_t parse_utc_ms(int argc, char **argv, int i)
{
    int y, mo, d, h, mi, s;
    if (argc >= i + 2 && sscanf(argv[i], "%d-%d-%d", &y, &mo, &d) == 3 &&
        sscanf(argv[i + 1], "%d:%d:%d", &h, &mi, &s) == 3) {
        // Julian day number of the civil date (no timegm in newlib).
        const int a = (14 - mo) / 12;
        const int64_t yy = y + 4800 - a;
        const int64_t mm = mo + 12 * a - 3;
        const int64_t jdn = d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
        return ((jdn - 2440588) * 86400 + h * 3600 + mi * 60 + s) * 1000;
    }
    if (argc >= i + 1) {
        char *end = NULL;
        const long long v = strtoll(argv[i], &end, 10);
        if (end && *end == '\0' && v > 0) {
            return v < 100000000000LL ? v * 1000 : v; // seconds or milliseconds
        }
    }
    return -1;
}

static int cmd_time(int argc, char **argv)
{
    const char *sub = argc >= 2 ? argv[1] : "";
    if (argc < 2) {
        return time_show();
    }
    if (strcmp(sub, "set") == 0 || strcmp(sub, "sync") == 0) {
        const int64_t ms = parse_utc_ms(argc, argv, 2);
        if (ms < 0) {
            printf("usage: time %s YYYY-MM-DD HH:MM:SS | <unix s|ms>   (UTC)\n", sub);
            return 1;
        }
        const svc_time_source_t src = sub[1] == 'y' ? SVC_TIME_SRC_PHONE : SVC_TIME_SRC_MANUAL;
        const esp_err_t err = svc_time_set_utc_ms(ms, src);
        if (err != ESP_OK) {
            printf("failed: %s (2000..2099 only)\n", esp_err_to_name(err));
            return 1;
        }
        return time_show();
    }
    if (strcmp(sub, "tz") == 0 && argc == 3) {
        if (svc_time_set_tz(argv[2]) != ESP_OK) {
            printf("invalid POSIX TZ string\n");
            return 1;
        }
        printf("zone set (applied by the settings event)\n");
        return 0;
    }
    if (strcmp(sub, "24h") == 0 && argc == 3) {
        return svc_time_set_24h(strcmp(argv[2], "on") == 0) == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "check") == 0) {
        const int64_t corr = svc_time_discipline_now();
        if (corr) {
            printf("system clock stepped %+lld ms to the RTC\n", (long long)corr);
        } else {
            printf("system clock agrees with the RTC (or time unknown / RTC write pending)\n");
        }
        return 0;
    }
    if (strcmp(sub, "local") == 0 && argc >= 3) { // tz_posix for any zone and instant
        tz_posix_t tz;
        if (!tz_posix_parse(argv[2], &tz)) {
            printf("invalid POSIX TZ string\n");
            return 1;
        }
        const int64_t t = argc >= 4 ? strtoll(argv[3], NULL, 10) : (int64_t)time(NULL);
        struct tm lt;
        tz_posix_localtime(&tz, t, &lt);
        char off[16];
        tz_posix_format_offset(tz_posix_offset(&tz, t, NULL), off, sizeof off);
        printf("%04d-%02d-%02d %02d:%02d:%02d %s%s\n", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour,
               lt.tm_min, lt.tm_sec, off, lt.tm_isdst ? " DST" : "");
        const int64_t next = tz_posix_next_transition(&tz, t);
        if (next != INT64_MAX) {
            print_utc("next transition ", (time_t)next);
        }
        return 0;
    }
    if (strcmp(sub, "drift") == 0 && argc == 3 && strcmp(argv[2], "reset") == 0) {
        return svc_time_drift_reset() == ESP_OK ? 0 : 1;
    }
    printf("usage: time [set|sync <YYYY-MM-DD HH:MM:SS | unix>] | tz <posix> | 24h on|off | check | "
           "local <posix> [unix] | drift reset\n");
    return 1;
}

esp_err_t diag_register_time(void)
{
    const esp_console_cmd_t cmd = {
        .command = "time",
        .help = "svc_time: no args = state (valid, source, zone, DST, drift). set <UTC> (manual), "
                "sync <UTC> (as a phone sync: calibrates drift), tz <posix>, 24h on|off, "
                "check (system clock vs RTC), local <posix> [unix] (any zone), drift reset",
        .hint = "[set|sync|tz|24h|check|local|drift]",
        .func = cmd_time,
    };
    return esp_console_cmd_register(&cmd);
}
