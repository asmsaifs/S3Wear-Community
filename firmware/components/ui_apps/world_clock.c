// World clock cities (world_clock.h). Pure C.
#include "world_clock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tz_posix.h"

// West to east. TZ strings as zic writes them for the current rules.
static const world_city_t CITIES[] = {
    {"honolulu", "Honolulu", "HNL", "HST10"},
    {"anchorage", "Anchorage", "ANC", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"los_angeles", "Los Angeles", "LA", "PST8PDT,M3.2.0,M11.1.0"},
    {"denver", "Denver", "DENVER", "MST7MDT,M3.2.0,M11.1.0"},
    {"chicago", "Chicago", "CHICAGO", "CST6CDT,M3.2.0,M11.1.0"},
    {"new_york", "New York", "NYC", "EST5EDT,M3.2.0,M11.1.0"},
    {"sao_paulo", "São Paulo", "SAO", "<-03>3"},
    {"reykjavik", "Reykjavik", "RVK", "GMT0"},
    {"london", "London", "LONDON", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"paris", "Paris", "PARIS", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"berlin", "Berlin", "BERLIN", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"cairo", "Cairo", "CAIRO", "EET-2EEST,M4.5.5/0,M10.5.4/24"},
    {"moscow", "Moscow", "MOSCOW", "MSK-3"},
    {"dubai", "Dubai", "DUBAI", "<+04>-4"},
    {"karachi", "Karachi", "KARACHI", "PKT-5"},
    {"delhi", "Delhi", "DELHI", "IST-5:30"},
    {"kathmandu", "Kathmandu", "KTM", "<+0545>-5:45"},
    {"dhaka", "Dhaka", "DHAKA", "<+06>-6"},
    {"bangkok", "Bangkok", "BANGKOK", "<+07>-7"},
    {"singapore", "Singapore", "SGP", "<+08>-8"},
    {"shanghai", "Shanghai", "SHA", "CST-8"},
    {"tokyo", "Tokyo", "TOKYO", "JST-9"},
    {"sydney", "Sydney", "SYDNEY", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"auckland", "Auckland", "AKL", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};
#define CITY_COUNT (sizeof CITIES / sizeof CITIES[0])

size_t world_city_count(void)
{
    return CITY_COUNT;
}

const world_city_t *world_city_at(size_t index)
{
    return index < CITY_COUNT ? &CITIES[index] : NULL;
}

const world_city_t *world_city_find(const char *id)
{
    for (size_t i = 0; id && i < CITY_COUNT; i++) {
        if (strcmp(CITIES[i].id, id) == 0) {
            return &CITIES[i];
        }
    }
    return NULL;
}

size_t world_clock_parse(const char *csv, const world_city_t **out, size_t max)
{
    size_t n = 0;
    while (csv && *csv && n < max) {
        const char *end = strchr(csv, ',');
        const size_t len = end ? (size_t)(end - csv) : strlen(csv);
        char id[24];
        if (len < sizeof id) {
            memcpy(id, csv, len);
            id[len] = '\0';
            const world_city_t *c = world_city_find(id);
            bool dup = false;
            for (size_t i = 0; c && i < n; i++) {
                dup |= out[i] == c;
            }
            if (c && !dup) {
                out[n++] = c;
            }
        }
        csv = end ? end + 1 : NULL;
    }
    return n;
}

void world_clock_join(const world_city_t *const *cities, size_t n, char *buf, size_t len)
{
    size_t pos = 0;
    if (len == 0) {
        return;
    }
    buf[0] = '\0';
    for (size_t i = 0; i < n && pos < len; i++) {
        pos += (size_t)snprintf(buf + pos, len - pos, "%s%s", i ? "," : "", cities[i]->id);
    }
}

int32_t world_city_offset(const world_city_t *c, int64_t utc)
{
    tz_posix_t tz;
    return tz_posix_parse(c->tz, &tz) ? tz_posix_offset(&tz, utc, NULL) : 0;
}

static int64_t day_of(int64_t local)
{
    return local >= 0 ? local / 86400 : (local - 86399) / 86400;
}

void world_clock_relative(int32_t city_off, int32_t home_off, int64_t utc, char *buf, size_t len)
{
    const int64_t days = day_of(utc + city_off) - day_of(utc + home_off);
    const char *day = days == 0 ? "Today" : days > 0 ? "Tomorrow" : "Yesterday";
    const int32_t diff = city_off - home_off;
    if (diff == 0) {
        snprintf(buf, len, "%s, same time", day);
        return;
    }
    const int32_t a = abs(diff);
    const char sign = diff > 0 ? '+' : '-';
    if (a % 3600) {
        snprintf(buf, len, "%s, %c%d:%02d h", day, sign, (int)(a / 3600), (int)(a % 3600 / 60));
    } else {
        snprintf(buf, len, "%s, %c%d h", day, sign, (int)(a / 3600));
    }
}
