// POSIX TZ string parser and converter. See tz_posix.h for the grammar.
#include "tz_posix.h"

#include <stdio.h>
#include <string.h>

#define SECS_PER_DAY  86400
#define MAX_OFFSET_H  24
#define MAX_RULE_H    167
#define DEFAULT_RULE_TIME (2 * 3600)

// --- Calendar (proleptic Gregorian, days since 1970-01-01; H. Hinnant's algorithms) ---

static bool is_leap(int64_t y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int days_in_month(int64_t y, int m)
{
    static const uint8_t dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && is_leap(y) ? 29 : dim[m - 1];
}

static int64_t days_from_civil(int64_t y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int64_t *y, int *m, int *d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = yoe + era * 400 + (*m <= 2);
}

static int64_t floor_div(int64_t a, int64_t b)
{
    return a / b - (a % b != 0 && (a < 0) != (b < 0));
}

static int weekday_from_days(int64_t z)
{
    return (int)(z >= -4 ? (z + 4) % 7 : (z + 5) % 7 + 6); // 1970-01-01 was a Thursday
}

void tz_posix_gmtime(int64_t utc, struct tm *out)
{
    const int64_t days = floor_div(utc, SECS_PER_DAY);
    const int64_t secs = utc - days * SECS_PER_DAY;
    int64_t y;
    int m;
    int d;
    civil_from_days(days, &y, &m, &d);
    memset(out, 0, sizeof *out);
    out->tm_year = (int)(y - 1900);
    out->tm_mon = m - 1;
    out->tm_mday = d;
    out->tm_hour = (int)(secs / 3600);
    out->tm_min = (int)(secs / 60 % 60);
    out->tm_sec = (int)(secs % 60);
    out->tm_wday = weekday_from_days(days);
    out->tm_yday = (int)(days - days_from_civil(y, 1, 1));
}

// --- Parser ---------------------------------------------------------------------------

static bool is_alpha(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool parse_name(const char **p, char *out)
{
    const char *s = *p;
    size_t n = 0;
    const bool quoted = *s == '<';
    if (quoted) {
        s++;
        while (is_alpha(s[n]) || is_digit(s[n]) || s[n] == '+' || s[n] == '-') {
            n++;
        }
        if (s[n] != '>') {
            return false;
        }
    } else {
        while (is_alpha(s[n])) {
            n++;
        }
    }
    if (n < 3 || n > TZ_POSIX_NAME_MAX) {
        return false;
    }
    memcpy(out, s, n);
    out[n] = '\0';
    *p = s + n + (quoted ? 1 : 0);
    return true;
}

static bool parse_num(const char **p, int max, int *out)
{
    const char *s = *p;
    int v = 0;
    int digits = 0;
    while (is_digit(*s)) {
        v = v * 10 + (*s++ - '0');
        if (++digits > 3 || v > max) {
            return false;
        }
    }
    if (digits == 0) {
        return false;
    }
    *p = s;
    *out = v;
    return true;
}

// [+|-]hh[:mm[:ss]] with hh <= max_h; returns signed seconds as written.
static bool parse_hms(const char **p, int max_h, int32_t *out)
{
    const char *s = *p;
    int sign = 1;
    if (*s == '+' || *s == '-') {
        sign = *s == '-' ? -1 : 1;
        s++;
    }
    int h = 0;
    int m = 0;
    int sec = 0;
    if (!parse_num(&s, max_h, &h)) {
        return false;
    }
    if (*s == ':') {
        s++;
        if (!parse_num(&s, 59, &m)) {
            return false;
        }
        if (*s == ':') {
            s++;
            if (!parse_num(&s, 59, &sec)) {
                return false;
            }
        }
    }
    *p = s;
    *out = sign * (h * 3600 + m * 60 + sec);
    return true;
}

static bool parse_rule(const char **p, tz_rule_t *r)
{
    const char *s = *p;
    int a = 0;
    memset(r, 0, sizeof *r);
    if (*s == 'J') {
        s++;
        if (!parse_num(&s, 365, &a) || a < 1) {
            return false;
        }
        r->kind = TZ_RULE_JULIAN_1;
        r->day = (uint16_t)a;
    } else if (*s == 'M') {
        int w = 0;
        int d = 0;
        s++;
        if (!parse_num(&s, 12, &a) || a < 1 || *s++ != '.' || !parse_num(&s, 5, &w) || w < 1 || *s++ != '.' ||
            !parse_num(&s, 6, &d)) {
            return false;
        }
        r->kind = TZ_RULE_MWD;
        r->month = (uint8_t)a;
        r->week = (uint8_t)w;
        r->wday = (uint8_t)d;
    } else {
        if (!parse_num(&s, 365, &a)) {
            return false;
        }
        r->kind = TZ_RULE_JULIAN_0;
        r->day = (uint16_t)a;
    }
    r->time_s = DEFAULT_RULE_TIME;
    if (*s == '/' && (s++, !parse_hms(&s, MAX_RULE_H, &r->time_s))) {
        return false;
    }
    *p = s;
    return true;
}

bool tz_posix_parse(const char *s, tz_posix_t *out)
{
    if (s == NULL) {
        return false;
    }
    tz_posix_t tz;
    memset(&tz, 0, sizeof tz);
    int32_t off = 0;
    if (!parse_name(&s, tz.std_name) || !parse_hms(&s, MAX_OFFSET_H, &off)) {
        return false;
    }
    tz.std_offset_s = -off;
    tz.dst_offset_s = tz.std_offset_s;
    if (*s != '\0') {
        if (!parse_name(&s, tz.dst_name)) {
            return false;
        }
        tz.has_dst = true;
        tz.dst_offset_s = tz.std_offset_s + 3600;
        if (*s != ',' && *s != '\0') {
            if (!parse_hms(&s, MAX_OFFSET_H, &off)) {
                return false;
            }
            tz.dst_offset_s = -off;
        }
        if (*s == ',') {
            s++;
            if (!parse_rule(&s, &tz.start) || *s++ != ',' || !parse_rule(&s, &tz.end)) {
                return false;
            }
        } else {
            static const char us_rules[] = "M3.2.0,M11.1.0";
            const char *r = us_rules;
            parse_rule(&r, &tz.start);
            r++;
            parse_rule(&r, &tz.end);
        }
        if (*s != '\0') {
            return false;
        }
    }
    *out = tz;
    return true;
}

// --- Conversion -----------------------------------------------------------------------

// Day of the rule in `year`, as days since the epoch.
static int64_t rule_day(const tz_rule_t *r, int64_t year)
{
    const int64_t jan1 = days_from_civil(year, 1, 1);
    switch (r->kind) {
    case TZ_RULE_JULIAN_1:
        return jan1 + r->day - 1 + (is_leap(year) && r->day >= 60 ? 1 : 0);
    case TZ_RULE_JULIAN_0:
        return jan1 + r->day;
    default: {
        const int64_t first = days_from_civil(year, r->month, 1);
        int mday = 1 + (r->wday - weekday_from_days(first) + 7) % 7 + (r->week - 1) * 7;
        while (mday > days_in_month(year, r->month)) {
            mday -= 7;
        }
        return first + mday - 1;
    }
    }
}

bool tz_posix_transitions(const tz_posix_t *tz, int year, int64_t *start_utc, int64_t *end_utc)
{
    if (!tz->has_dst) {
        return false;
    }
    *start_utc = rule_day(&tz->start, year) * SECS_PER_DAY + tz->start.time_s - tz->std_offset_s;
    *end_utc = rule_day(&tz->end, year) * SECS_PER_DAY + tz->end.time_s - tz->dst_offset_s;
    return true;
}

int32_t tz_posix_offset(const tz_posix_t *tz, int64_t utc, bool *is_dst)
{
    bool dst = false;
    if (tz->has_dst) {
        int64_t y;
        int m;
        int d;
        civil_from_days(floor_div(utc + tz->std_offset_s, SECS_PER_DAY), &y, &m, &d);
        int64_t start;
        int64_t end;
        tz_posix_transitions(tz, (int)y, &start, &end);
        if (start < end) { // northern hemisphere: DST inside the year
            dst = utc >= start && utc < end;
        } else {           // southern: DST spans the new year
            dst = !(utc >= end && utc < start);
        }
    }
    if (is_dst) {
        *is_dst = dst;
    }
    return dst ? tz->dst_offset_s : tz->std_offset_s;
}

void tz_posix_localtime(const tz_posix_t *tz, int64_t utc, struct tm *out)
{
    bool dst = false;
    const int32_t off = tz_posix_offset(tz, utc, &dst);
    tz_posix_gmtime(utc + off, out);
    out->tm_isdst = dst ? 1 : 0;
}

// Next rule instant (start or end) strictly after utc.
static int64_t next_rule_instant(const tz_posix_t *tz, int64_t utc)
{
    int64_t y;
    int m;
    int d;
    civil_from_days(floor_div(utc + tz->std_offset_s, SECS_PER_DAY), &y, &m, &d);
    int64_t best = INT64_MAX;
    for (int64_t yy = y - 1; yy <= y + 1; yy++) { // rule times of +/-167 h can cross a year
        int64_t s;
        int64_t e;
        tz_posix_transitions(tz, (int)yy, &s, &e);
        if (s > utc && s < best) {
            best = s;
        }
        if (e > utc && e < best) {
            best = e;
        }
    }
    return best;
}

int64_t tz_posix_next_transition(const tz_posix_t *tz, int64_t utc)
{
    if (!tz->has_dst) {
        return INT64_MAX;
    }
    // A rule instant that does not change the offset (all-year DST written as
    // Jan 1 .. Dec 31 + 25 h) is not a transition: look past it, a few years at most.
    const int32_t now_off = tz_posix_offset(tz, utc, NULL);
    int64_t t = utc;
    for (int i = 0; i < 8; i++) {
        t = next_rule_instant(tz, t);
        if (t == INT64_MAX || tz_posix_offset(tz, t, NULL) != now_off) {
            return t;
        }
    }
    return INT64_MAX;
}

void tz_posix_fixed_string(const tz_posix_t *tz, int64_t utc, char *buf, size_t len)
{
    bool dst = false;
    const int32_t east = tz_posix_offset(tz, utc, &dst);
    const char *name = dst ? tz->dst_name : tz->std_name;
    const size_t n = strlen(name);
    bool alpha = true;
    for (size_t i = 0; i < n; i++) {
        alpha = alpha && is_alpha(name[i]);
    }
    if (n < 3 || n > 10) {
        name = "LCL";
        alpha = true;
    }
    const int32_t west = -east;
    const int32_t a = west < 0 ? -west : west;
    char off[16];
    const int h = (int)(a / 3600);
    const int mi = (int)(a / 60 % 60);
    const int s = (int)(a % 60);
    if (s) {
        snprintf(off, sizeof off, "%s%d:%02d:%02d", west < 0 ? "-" : "", h, mi, s);
    } else if (mi) {
        snprintf(off, sizeof off, "%s%d:%02d", west < 0 ? "-" : "", h, mi);
    } else {
        snprintf(off, sizeof off, "%s%d", west < 0 ? "-" : "", h);
    }
    snprintf(buf, len, alpha ? "%s%s" : "<%s>%s", name, off);
}

void tz_posix_format_offset(int32_t offset_s, char *buf, size_t len)
{
    if (offset_s == 0) {
        snprintf(buf, len, "UTC");
        return;
    }
    const char sign = offset_s < 0 ? '-' : '+';
    const int32_t a = offset_s < 0 ? -offset_s : offset_s;
    const int h = (int)(a / 3600);
    const int m = (int)(a / 60 % 60);
    if (m) {
        snprintf(buf, len, "UTC%c%d:%02d", sign, h, m);
    } else {
        snprintf(buf, len, "UTC%c%d", sign, h);
    }
}
