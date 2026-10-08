// Binding registry and formatters (wf_bind.h). Pure C.
#include "wf_bind.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI 3.14159265358979323846
#define CHARGE_SYMBOL "\xEF\x83\xA7" // LV_SYMBOL_CHARGE (in the UI text fonts)

typedef enum {
    K_TIME_HHMM,
    K_TIME_HH,
    K_TIME_MM,
    K_TIME_SS,
    K_TIME_AMPM,
    K_TIME_HOUR,
    K_TIME_MINUTE,
    K_TIME_SECOND,
    K_DATE,
    K_BATTERY_PERCENT,
    K_BATTERY_RATIO,
    K_BATTERY_CHARGING,
    K_STEPS_COUNT,
    K_STEPS_GOAL,
    K_STEPS_GOAL_RATIO,
    K_WEATHER_TEMP,
    K_WEATHER_LOW,
    K_WEATHER_HIGH,
    K_WEATHER_CONDITION,
    K_PHONE_BATTERY,
    K_EVENT_TITLE,
    K_EVENT_TIME,
    K_HEART_BPM,
    K_NOTIFY_COUNT,
    K_MOON_PHASE,
    K_MOON_ILLUMINATION,
    K_COUNT,
} bind_key_t;

typedef struct {
    const char *name;
    uint32_t deps;
} key_info_t;

static const key_info_t KEYS[K_COUNT] = {
    [K_TIME_HHMM] = {"time.hh:mm", WF_DATA_TIME},
    [K_TIME_HH] = {"time.hh", WF_DATA_TIME},
    [K_TIME_MM] = {"time.mm", WF_DATA_TIME},
    [K_TIME_SS] = {"time.ss", WF_DATA_TIME | WF_DATA_SECOND},
    [K_TIME_AMPM] = {"time.ampm", WF_DATA_TIME},
    [K_TIME_HOUR] = {"time.hour", WF_DATA_TIME},
    [K_TIME_MINUTE] = {"time.minute", WF_DATA_TIME | WF_DATA_SECOND},
    [K_TIME_SECOND] = {"time.second", WF_DATA_TIME | WF_DATA_SECOND},
    [K_DATE] = {"date.", WF_DATA_TIME},
    [K_BATTERY_PERCENT] = {"battery.percent", WF_DATA_BATTERY},
    [K_BATTERY_RATIO] = {"battery.ratio", WF_DATA_BATTERY},
    [K_BATTERY_CHARGING] = {"battery.charging", WF_DATA_BATTERY},
    [K_STEPS_COUNT] = {"steps.count", WF_DATA_STEPS},
    [K_STEPS_GOAL] = {"steps.goal", WF_DATA_STEPS},
    [K_STEPS_GOAL_RATIO] = {"steps.goal_ratio", WF_DATA_STEPS},
    [K_WEATHER_TEMP] = {"weather.temp", WF_DATA_WEATHER},
    [K_WEATHER_LOW] = {"weather.low", WF_DATA_WEATHER},
    [K_WEATHER_HIGH] = {"weather.high", WF_DATA_WEATHER},
    [K_WEATHER_CONDITION] = {"weather.condition", WF_DATA_WEATHER},
    [K_PHONE_BATTERY] = {"phone.battery", WF_DATA_PHONE},
    [K_EVENT_TITLE] = {"next_event.title", WF_DATA_EVENT},
    [K_EVENT_TIME] = {"next_event.time", WF_DATA_EVENT | WF_DATA_TIME},
    [K_HEART_BPM] = {"heart.bpm", WF_DATA_HEART},
    [K_NOTIFY_COUNT] = {"notifications.count", WF_DATA_NOTIFY},
    [K_MOON_PHASE] = {"moon.phase", WF_DATA_TIME},
    [K_MOON_ILLUMINATION] = {"moon.illumination", WF_DATA_TIME},
};

static const char *const WEEKDAY[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *const MONTH[12] = {"January", "February", "March",     "April",   "May",      "June",
                                      "July",    "August",   "September", "October", "November", "December"};
static const char *const CONDITION[WF_WEATHER_COUNT] = {"Clear", "Cloudy", "Rain", "Snow", "Storm", "Fog",
                                                               "Partly cloudy"};

// --- Formatters ------------------------------------------------------------------------

/** Append src (at most n bytes of it) to buf of size len at *pos. */
static void put(char *buf, size_t len, size_t *pos, const char *src, size_t n)
{
    while (n-- > 0 && *src && *pos + 1 < len) {
        buf[(*pos)++] = *src++;
    }
    buf[*pos < len ? *pos : len - 1] = '\0';
}

bool wf_format_date(const char *pattern, const struct tm *tm, char *buf, size_t len)
{
    if (len == 0) {
        return false;
    }
    size_t pos = 0;
    buf[0] = '\0';
    for (const char *p = pattern; *p;) {
        const char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
            put(buf, len, &pos, p, 1);
            p++;
            continue;
        }
        size_t run = 1;
        while (p[run] == c) {
            run++;
        }
        char num[8];
        const int wday = tm->tm_wday >= 0 && tm->tm_wday < 7 ? tm->tm_wday : 0;
        const int mon = tm->tm_mon >= 0 && tm->tm_mon < 12 ? tm->tm_mon : 0;
        if (c == 'E' && (run == 3 || run == 4)) {
            put(buf, len, &pos, WEEKDAY[wday], run == 3 ? 3 : SIZE_MAX);
        } else if (c == 'd' && run <= 2) {
            snprintf(num, sizeof num, run == 1 ? "%d" : "%02d", tm->tm_mday);
            put(buf, len, &pos, num, SIZE_MAX);
        } else if (c == 'M' && run <= 4) {
            if (run >= 3) {
                put(buf, len, &pos, MONTH[mon], run == 3 ? 3 : SIZE_MAX);
            } else {
                snprintf(num, sizeof num, run == 1 ? "%d" : "%02d", mon + 1);
                put(buf, len, &pos, num, SIZE_MAX);
            }
        } else if (c == 'y' && (run == 2 || run == 4)) {
            const int year = tm->tm_year + 1900;
            snprintf(num, sizeof num, run == 2 ? "%02d" : "%04d", run == 2 ? year % 100 : year);
            put(buf, len, &pos, num, SIZE_MAX);
        } else {
            buf[0] = '\0';
            return false;
        }
        p += run;
    }
    return true;
}

void wf_format_hm(int hour, int minute, bool h24, char *buf, size_t len)
{
    if (h24) {
        snprintf(buf, len, "%02d:%02d", hour, minute);
    } else {
        const int h = hour % 12;
        snprintf(buf, len, "%d:%02d", h == 0 ? 12 : h, minute);
    }
}

void wf_format_thousands(int32_t v, char *buf, size_t len)
{
    char digits[16];
    const int64_t mag = v < 0 ? -(int64_t)v : v;
    const int n = snprintf(digits, sizeof digits, "%lld", (long long)mag);
    size_t pos = 0;
    if (len == 0) {
        return;
    }
    buf[0] = '\0';
    if (v < 0) {
        put(buf, len, &pos, "-", 1);
    }
    for (int i = 0; i < n; i++) {
        if (i > 0 && (n - i) % 3 == 0) {
            put(buf, len, &pos, ",", 1);
        }
        put(buf, len, &pos, &digits[i], 1);
    }
}

// --- Moon ------------------------------------------------------------------------------

#define MOON_REF_NEW  ((int64_t)947182440) // new moon 2000-01-06 18:14 UTC
#define MOON_SYNODIC  ((int64_t)2551443)   // mean synodic month, s (29.530589 d)

int32_t wf_moon_cycle(time_t t)
{
    int64_t age = ((int64_t)t - MOON_REF_NEW) % MOON_SYNODIC;
    if (age < 0) {
        age += MOON_SYNODIC;
    }
    return (int32_t)(age * 1000 / MOON_SYNODIC);
}

int32_t wf_moon_illumination(int32_t cycle)
{
    const double k = (1.0 - cos(2.0 * PI * cycle / 1000.0)) / 2.0;
    return (int32_t)(k * 1000.0 + 0.5);
}

const char *wf_moon_phase_name(int32_t cycle)
{
    static const char *const NAMES[8] = {"New moon",  "Waxing crescent", "First quarter", "Waxing gibbous",
                                         "Full moon", "Waning gibbous",  "Last quarter",  "Waning crescent"};
    return NAMES[((cycle % 1000 + 1000) % 1000 + 62) / 125 % 8];
}

// --- Bindings --------------------------------------------------------------------------

bool wf_bind_parse(const char *name, wf_bind_t *out)
{
    if (name == NULL || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof *out);
    if (strncmp(name, "date.", 5) == 0) {
        const char *pattern = name + 5;
        char probe[64];
        static const struct tm TM = {.tm_mday = 1, .tm_year = 126};
        if (*pattern == '\0' || strlen(pattern) >= WF_BIND_PATTERN_MAX ||
            !wf_format_date(pattern, &TM, probe, sizeof probe)) {
            return false;
        }
        out->key = K_DATE;
        strcpy(out->pattern, pattern);
        return true;
    }
    for (int k = 0; k < K_COUNT; k++) {
        if (k != K_DATE && strcmp(name, KEYS[k].name) == 0) {
            out->key = (uint8_t)k;
            return true;
        }
    }
    return false;
}

bool wf_bind_has_text(const wf_bind_t *b)
{
    return b->key < K_COUNT && b->key != K_TIME_HOUR && b->key != K_TIME_MINUTE && b->key != K_TIME_SECOND;
}

bool wf_bind_has_value(const wf_bind_t *b)
{
    return b->key < K_COUNT && b->key != K_EVENT_TITLE;
}

uint32_t wf_bind_deps(const wf_bind_t *b)
{
    return b->key < K_COUNT ? KEYS[b->key].deps : 0;
}

static int clamp_ratio(int64_t num, int64_t den)
{
    if (den <= 0 || num <= 0) {
        return 0;
    }
    const int64_t r = num * 1000 / den;
    return r > 1000 ? 1000 : (int)r;
}

static bool time_key(int key)
{
    return key <= K_DATE || key == K_EVENT_TIME;
}

bool wf_bind_value(const wf_bind_t *b, const wf_ctx_t *ctx, int32_t *out)
{
    const wf_data_t *d = ctx->data;
    const struct tm *tm = &ctx->tm;
    if (time_key(b->key) && !ctx->time_valid) {
        return false;
    }
    const int32_t hms12 = (tm->tm_hour % 12) * 3600 + tm->tm_min * 60 + tm->tm_sec;
    switch (b->key) {
    case K_TIME_HHMM:
        *out = tm->tm_hour * 60 + tm->tm_min;
        return true;
    case K_TIME_HH:
        *out = ctx->h24 ? tm->tm_hour : (tm->tm_hour % 12 == 0 ? 12 : tm->tm_hour % 12);
        return true;
    case K_TIME_MM:
        *out = tm->tm_min;
        return true;
    case K_TIME_SS:
        *out = tm->tm_sec;
        return true;
    case K_TIME_AMPM:
        *out = tm->tm_hour >= 12;
        return true;
    case K_TIME_HOUR:
        *out = hms12 / 12; // 43200 s per turn = 3600 tenths of a degree
        return true;
    case K_TIME_MINUTE:
        *out = tm->tm_min * 60 + tm->tm_sec;
        return true;
    case K_TIME_SECOND:
        *out = tm->tm_sec * 60;
        return true;
    case K_DATE:
        *out = tm->tm_mday;
        return true;
    case K_BATTERY_PERCENT:
        *out = d->battery_pct;
        return d->battery_pct >= 0;
    case K_BATTERY_RATIO:
        *out = d->battery_pct * 10;
        return d->battery_pct >= 0;
    case K_BATTERY_CHARGING:
        *out = d->charging;
        return d->battery_pct >= 0;
    case K_STEPS_COUNT:
        *out = d->steps;
        return d->steps >= 0;
    case K_STEPS_GOAL:
        *out = d->steps_goal;
        return d->steps_goal > 0;
    case K_STEPS_GOAL_RATIO:
        *out = clamp_ratio(d->steps, d->steps_goal);
        return d->steps >= 0 && d->steps_goal > 0;
    case K_WEATHER_TEMP:
        *out = d->temp_c;
        return d->weather_valid;
    case K_WEATHER_LOW:
        *out = d->temp_lo_c;
        return d->weather_range;
    case K_WEATHER_HIGH:
        *out = d->temp_hi_c;
        return d->weather_range;
    case K_WEATHER_CONDITION:
        *out = d->weather;
        return d->weather_valid && d->weather < WF_WEATHER_COUNT;
    case K_PHONE_BATTERY:
        *out = d->phone_battery_pct;
        return d->phone_battery_pct >= 0;
    case K_EVENT_TIME: {
        if (!d->event_valid) {
            return false;
        }
        struct tm et;
        localtime_r(&d->event_start, &et);
        *out = et.tm_hour * 60 + et.tm_min;
        return true;
    }
    case K_HEART_BPM:
        *out = d->heart_bpm;
        return d->heart_bpm > 0;
    case K_NOTIFY_COUNT:
        *out = d->notifications;
        return d->notifications >= 0;
    case K_MOON_PHASE:
        *out = wf_moon_cycle(ctx->now);
        return true;
    case K_MOON_ILLUMINATION:
        *out = wf_moon_illumination(wf_moon_cycle(ctx->now));
        return true;
    default:
        return false;
    }
}

bool wf_bind_text(const wf_bind_t *b, const wf_ctx_t *ctx, char *buf, size_t len)
{
    if (len == 0) {
        return false;
    }
    const wf_data_t *d = ctx->data;
    int32_t v = 0;
    bool ok = b->key == K_EVENT_TITLE ? d->event_valid : wf_bind_value(b, ctx, &v);
    if (!ok) {
        snprintf(buf, len, "%s", b->key == K_TIME_HHMM || b->key == K_EVENT_TIME ? "--:--" : "--");
        return false;
    }
    switch (b->key) {
    case K_TIME_HHMM:
        wf_format_hm(ctx->tm.tm_hour, ctx->tm.tm_min, ctx->h24, buf, len);
        break;
    case K_TIME_HH:
        snprintf(buf, len, ctx->h24 ? "%02ld" : "%ld", (long)v);
        break;
    case K_TIME_MM:
    case K_TIME_SS:
        snprintf(buf, len, "%02ld", (long)v);
        break;
    case K_TIME_AMPM:
        snprintf(buf, len, "%s", ctx->h24 ? "" : (v ? "PM" : "AM"));
        break;
    case K_TIME_HOUR:
    case K_TIME_MINUTE:
    case K_TIME_SECOND:
        buf[0] = '\0'; // angles have no text form
        return false;
    case K_DATE:
        wf_format_date(b->pattern, &ctx->tm, buf, len);
        break;
    case K_BATTERY_RATIO:
    case K_STEPS_GOAL_RATIO:
        snprintf(buf, len, "%ld%%", (long)(v / 10));
        break;
    case K_BATTERY_CHARGING:
        snprintf(buf, len, "%s", v ? CHARGE_SYMBOL : "");
        break;
    case K_STEPS_COUNT:
    case K_STEPS_GOAL:
        wf_format_thousands(v, buf, len);
        break;
    case K_WEATHER_TEMP:
    case K_WEATHER_LOW:
    case K_WEATHER_HIGH:
        snprintf(buf, len, "%ld\xC2\xB0", (long)v); // degree sign
        break;
    case K_WEATHER_CONDITION:
        snprintf(buf, len, "%s", CONDITION[v]);
        break;
    case K_EVENT_TITLE:
        snprintf(buf, len, "%s", d->event_title);
        break;
    case K_EVENT_TIME:
        wf_format_hm((int)(v / 60), (int)(v % 60), ctx->h24, buf, len);
        break;
    case K_MOON_PHASE:
        snprintf(buf, len, "%s", wf_moon_phase_name(v));
        break;
    case K_MOON_ILLUMINATION:
        snprintf(buf, len, "%ld%%", (long)((v + 5) / 10));
        break;
    default:
        snprintf(buf, len, "%ld", (long)v);
        break;
    }
    return true;
}

// --- Data ------------------------------------------------------------------------------

void wf_data_init(wf_data_t *d)
{
    memset(d, 0, sizeof *d);
    d->battery_pct = -1;
    d->steps = -1;
    d->steps_goal = 10000;
    d->phone_battery_pct = -1;
    d->sunrise_min = -1;
    d->sunset_min = -1;
    d->notifications = -1;
}

void wf_ctx_init(wf_ctx_t *ctx, const wf_data_t *data, time_t now, bool h24, bool time_valid)
{
    ctx->data = data;
    ctx->now = now;
    localtime_r(&now, &ctx->tm);
    ctx->h24 = h24;
    ctx->time_valid = time_valid;
}

const char *wf_weather_name(uint8_t w)
{
    return w < WF_WEATHER_COUNT ? CONDITION[w] : "";
}
