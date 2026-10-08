// Console: `rtc get|set|alarm|offset` — PCF85063 bring-up checks (P1-04). Times are UTC.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "bsp_s3w.h"
#include "esp_console.h"
#include "esp_log.h"
#include "svc_diag_priv.h"

static const char *TAG = "cmd_rtc";

static void print_tm(const char *label, const struct tm *tm)
{
    printf("%s %04d-%02d-%02d %02d:%02d:%02d UTC\n", label, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
           tm->tm_hour, tm->tm_min, tm->tm_sec);
}

static void on_alarm(void *ctx)
{
    (void)ctx;
    struct tm now;
    bool os = false;
    if (pcf85063_get_time(bsp_rtc_handle(), &now, &os) == ESP_OK) {
        ESP_LOGI(TAG, "RTC alarm interrupt at %02d:%02d:%02d UTC", now.tm_hour, now.tm_min, now.tm_sec);
    } else {
        ESP_LOGI(TAG, "RTC alarm interrupt");
    }
}

static int rtc_get(pcf85063_handle_t rtc)
{
    struct tm tm;
    bool os = false;
    if (pcf85063_get_time(rtc, &tm, &os) != ESP_OK) {
        printf("RTC read failed\n");
        return 1;
    }
    print_tm("rtc   ", &tm);
    printf("oscillator-stop flag: %s\n", os ? "SET (time invalid)" : "clear");
    int8_t steps = 0;
    bool coarse = false;
    if (pcf85063_get_offset(rtc, &steps, &coarse) == ESP_OK) {
        printf("offset: %d steps (%s mode)\n", steps, coarse ? "coarse" : "normal");
    }
    const time_t now = time(NULL);
    struct tm sys;
    gmtime_r(&now, &sys);
    print_tm("system", &sys);
    return 0;
}

static int rtc_set(pcf85063_handle_t rtc, int argc, char **argv)
{
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    if (argc >= 4) {
        int y, mo, d, h, mi, s;
        if (sscanf(argv[2], "%d-%d-%d", &y, &mo, &d) != 3 || sscanf(argv[3], "%d:%d:%d", &h, &mi, &s) != 3) {
            printf("usage: rtc set YYYY-MM-DD HH:MM:SS\n");
            return 1;
        }
        tm.tm_year = y - 1900;
        tm.tm_mon = mo - 1;
        tm.tm_mday = d;
        tm.tm_hour = h;
        tm.tm_min = mi;
        tm.tm_sec = s;
        // Normalise and derive the weekday without depending on TZ.
        const time_t t = (time_t)pcf85063_tm_to_unix(&tm);
        gmtime_r(&t, &tm);
        const struct timeval tv = {.tv_sec = t};
        settimeofday(&tv, NULL);
    } else {
        const time_t now = time(NULL); // copy system time into the RTC
        gmtime_r(&now, &tm);
    }
    if (pcf85063_set_time(rtc, &tm) != ESP_OK) {
        printf("RTC write failed (year must be 2000..2099)\n");
        return 1;
    }
    print_tm("set", &tm);
    return 0;
}

static int rtc_alarm(pcf85063_handle_t rtc, int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[2], "off") == 0) {
        return pcf85063_disable_alarm(rtc) == ESP_OK ? 0 : 1;
    }
    if (argc < 3 || argv[2][0] != '+') {
        printf("usage: rtc alarm +<seconds> | off\n");
        return 1;
    }
    const int secs = atoi(argv[2] + 1);
    struct tm now;
    bool os = false;
    if (secs <= 0 || pcf85063_get_time(rtc, &now, &os) != ESP_OK) {
        printf("bad delay or RTC read failed\n");
        return 1;
    }
    const time_t at_t = (time_t)pcf85063_tm_to_unix(&now) + secs;
    struct tm at;
    gmtime_r(&at_t, &at);
    bsp_rtc_set_alarm_cb(on_alarm, NULL);
    if (pcf85063_set_alarm(rtc, &at) != ESP_OK) {
        printf("alarm write failed\n");
        return 1;
    }
    print_tm("alarm set for", &at);
    printf("(bring-up test: replaces svc_alarm's RTC alarm and callback until reboot)\n");
    return 0;
}

static int rtc_offset(pcf85063_handle_t rtc, int argc, char **argv)
{
    if (argc >= 3) {
        const int steps = atoi(argv[2]);
        const bool coarse = argc >= 4 && strcmp(argv[3], "coarse") == 0;
        if (steps < -64 || steps > 63) {
            printf("steps must be -64..63\n");
            return 1;
        }
        if (pcf85063_set_offset(rtc, (int8_t)steps, coarse) != ESP_OK) {
            return 1;
        }
    }
    int8_t steps = 0;
    bool coarse = false;
    if (pcf85063_get_offset(rtc, &steps, &coarse) != ESP_OK) {
        return 1;
    }
    printf("offset: %d steps (%s, %s ppm/step)\n", steps, coarse ? "coarse" : "normal", coarse ? "4.069" : "4.34");
    return 0;
}

static int cmd_rtc(int argc, char **argv)
{
    pcf85063_handle_t rtc = bsp_rtc_handle();
    if (!rtc) {
        printf("RTC not initialised\n");
        return 1;
    }
    const char *sub = argc >= 2 ? argv[1] : "";
    if (strcmp(sub, "get") == 0) {
        return rtc_get(rtc);
    }
    if (strcmp(sub, "set") == 0) {
        return rtc_set(rtc, argc, argv);
    }
    if (strcmp(sub, "alarm") == 0) {
        return rtc_alarm(rtc, argc, argv);
    }
    if (strcmp(sub, "offset") == 0) {
        return rtc_offset(rtc, argc, argv);
    }
    printf("usage: rtc get | set [YYYY-MM-DD HH:MM:SS] | alarm +<s>|off | offset [steps [coarse]]\n");
    return 1;
}

esp_err_t diag_register_rtc(void)
{
    const esp_console_cmd_t cmd = {
        .command = "rtc",
        .help = "PCF85063 (UTC): get, set [YYYY-MM-DD HH:MM:SS] (no args = from system time), "
                "alarm +<s>|off (INT on GPIO39 is logged), offset [steps [coarse]]",
        .hint = "get|set|alarm|offset",
        .func = cmd_rtc,
    };
    return esp_console_cmd_register(&cmd);
}
