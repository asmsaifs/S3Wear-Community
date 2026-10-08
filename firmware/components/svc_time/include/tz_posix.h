// POSIX TZ strings (pure logic, no ESP-IDF: host-tested).
//
// The phone sends the zone as a POSIX TZ string ("CET-1CEST,M3.5.0,M10.5.0/3",
// "<+06>-6"). svc_time validates it here before handing it to newlib (setenv/tzset),
// which does the system localtime. This parser also converts in any zone without
// touching the global TZ (world clock, alarms in another zone, console checks).
//
// Grammar (POSIX.1-2024 TZ, plus the RFC 8536 extensions that zic emits):
//   std offset [dst [offset] [,start[/time],end[/time]]]
//   name   : 3..15 letters, or <3..15 of [A-Za-z0-9+-]>
//   offset : [+|-]hh[:mm[:ss]], hh 0..24; positive = WEST of Greenwich
//   rule   : Jn (1..365, Feb 29 never counted) | n (0..365, counts Feb 29) | Mm.w.d
//            (month 1..12, week 1..5 with 5 = last, weekday 0..6 Sunday first)
//   time   : [+|-]hhh[:mm[:ss]], -167..167 h, default 02:00:00, in local time
//            (start in standard time, end in daylight time)
// A dst name with no rules uses the US rules (M3.2.0,M11.1.0), as glibc does.
// Colon-prefixed names (":Europe/Paris") are not supported: there is no tz database.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TZ_POSIX_NAME_MAX 15

typedef enum {
    TZ_RULE_JULIAN_1 = 0, // Jn: 1..365, Feb 29 is never counted
    TZ_RULE_JULIAN_0,     // n:  0..365, Feb 29 counted in leap years
    TZ_RULE_MWD,          // Mm.w.d
} tz_rule_kind_t;

typedef struct {
    uint8_t kind;   // tz_rule_kind_t
    uint8_t month;  // MWD: 1..12
    uint8_t week;   // MWD: 1..5 (5 = last)
    uint8_t wday;   // MWD: 0..6 (Sunday = 0)
    uint16_t day;   // Jn / n
    int32_t time_s; // seconds after local midnight, -167 h..167 h
} tz_rule_t;

typedef struct {
    char std_name[TZ_POSIX_NAME_MAX + 1];
    char dst_name[TZ_POSIX_NAME_MAX + 1]; // "" when has_dst is false
    int32_t std_offset_s;                  // seconds EAST of UTC (sign flipped from the string)
    int32_t dst_offset_s;
    bool has_dst;
    tz_rule_t start; // standard -> daylight, in local standard time
    tz_rule_t end;   // daylight -> standard, in local daylight time
} tz_posix_t;

/** Parse the whole string. false (out untouched) on any syntax or range error. */
bool tz_posix_parse(const char *s, tz_posix_t *out);

/** UTC offset in seconds east at the UTC instant utc; *is_dst may be NULL. */
int32_t tz_posix_offset(const tz_posix_t *tz, int64_t utc, bool *is_dst);

/** Local broken-down time (tm_isdst, tm_wday, tm_yday set) for the UTC instant utc. */
void tz_posix_localtime(const tz_posix_t *tz, int64_t utc, struct tm *out);

/** UTC broken-down time without newlib (no timegm on the watch). */
void tz_posix_gmtime(int64_t utc, struct tm *out);

/**
 * UTC instants of the daylight saving start and end in the calendar year `year`
 * (the year of the local standard time). false if the zone has no DST.
 */
bool tz_posix_transitions(const tz_posix_t *tz, int year, int64_t *start_utc, int64_t *end_utc);

/** First DST transition strictly after utc, or INT64_MAX if the zone has none. */
int64_t tz_posix_next_transition(const tz_posix_t *tz, int64_t utc);

/**
 * The zone as it is at utc, as a fixed-offset TZ string without rules ("<+06>-6",
 * "CEST-2"), for newlib: newlib's tzset rejects some valid rules (negative or > 24 h
 * rule times) and then silently keeps the previous zone, so svc_time gives it only
 * the current offset and re-applies it at every transition. Names that are not 3..10
 * characters (newlib's limit) become "LCL". buf needs TZ_POSIX_FIXED_MAX bytes.
 */
#define TZ_POSIX_FIXED_MAX 24
void tz_posix_fixed_string(const tz_posix_t *tz, int64_t utc, char *buf, size_t len);

/** "UTC", "UTC+6", "UTC-3:30", "UTC+5:45" into buf (at least 10 bytes). */
void tz_posix_format_offset(int32_t offset_s, char *buf, size_t len);

#ifdef __cplusplus
}
#endif
