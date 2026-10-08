// Alarm scheduler (alarm_sched.h). Pure C: no ESP-IDF, no newlib time zone.
#include "alarm_sched.h"

#include <stdio.h>
#include <string.h>

#define BLOB_MAGIC0  'A'
#define BLOB_MAGIC1  'L'
#define BLOB_VERSION 1
#define BLOB_HEADER  24
#define BLOB_ITEM    (6 + ALARM_LABEL_MAX + 1)

_Static_assert(ALARM_BLOB_MAX == BLOB_HEADER + ALARM_MAX * BLOB_ITEM + 4, "blob size");

// Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant's algorithm).
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int weekday_of(int64_t days)
{
    const int64_t w = (days + 4) % 7; // 1970-01-01 was a Thursday
    return (int)(w < 0 ? w + 7 : w);
}

// Local seconds (since the local epoch) -> UTC, with the gap/overlap rule of the header.
static int64_t local_to_utc(const tz_posix_t *tz, int64_t local)
{
    const int32_t off_a = tz->std_offset_s;
    const int32_t off_b = tz->has_dst ? tz->dst_offset_s : tz->std_offset_s;
    int64_t best = ALARM_NEVER;
    const int32_t offs[2] = {off_a, off_b};
    for (int i = 0; i < 2; i++) {
        const int64_t u = local - offs[i];
        if (tz_posix_offset(tz, u, NULL) == offs[i] && u < best) {
            best = u; // the earlier one if the local time happens twice
        }
    }
    if (best == ALARM_NEVER) {
        // In a gap: the clock jumped from the smaller offset to the larger one.
        best = local - (off_a < off_b ? off_a : off_b);
    }
    return best;
}

int64_t alarm_local_to_utc(const tz_posix_t *tz, int year, int month, int mday, int hour, int minute)
{
    return local_to_utc(tz, days_from_civil(year, month, mday) * 86400 + hour * 3600 + minute * 60);
}

void alarm_set_init(alarm_set_t *set)
{
    memset(set, 0, sizeof *set);
}

bool alarm_valid(const alarm_t *a)
{
    return a->hour < 24 && a->minute < 60 && a->days <= ALARM_DAYS_ALL && a->snooze_min >= ALARM_SNOOZE_MIN &&
           a->snooze_min <= ALARM_SNOOZE_MAX && memchr(a->label, '\0', sizeof a->label) != NULL;
}

void alarm_default(alarm_t *a, uint8_t hour, uint8_t minute)
{
    memset(a, 0, sizeof *a);
    a->enabled = true;
    a->hour = hour;
    a->minute = minute;
    a->snooze_min = ALARM_SNOOZE_DEFAULT;
}

static int cmp(const alarm_t *x, const alarm_t *y)
{
    const int tx = x->hour * 60 + x->minute;
    const int ty = y->hour * 60 + y->minute;
    return tx != ty ? tx - ty : (int)x->id - (int)y->id;
}

static void sort(alarm_set_t *set)
{
    for (int i = 1; i < set->count; i++) {
        const alarm_t a = set->items[i];
        int j = i - 1;
        for (; j >= 0 && cmp(&set->items[j], &a) > 0; j--) {
            set->items[j + 1] = set->items[j];
        }
        set->items[j + 1] = a;
    }
}

alarm_t *alarm_set_find(alarm_set_t *set, uint8_t id)
{
    for (int i = 0; id && i < set->count; i++) {
        if (set->items[i].id == id) {
            return &set->items[i];
        }
    }
    return NULL;
}

const alarm_t *alarm_set_find_const(const alarm_set_t *set, uint8_t id)
{
    return alarm_set_find((alarm_set_t *)set, id);
}

uint8_t alarm_set_put(alarm_set_t *set, const alarm_t *a)
{
    if (!alarm_valid(a)) {
        return 0;
    }
    alarm_t *slot;
    uint8_t id = a->id;
    if (id == 0) {
        if (set->count >= ALARM_MAX) {
            return 0;
        }
        id = set->last_id;
        do {
            id++; // wraps 255 -> 0 -> 1
        } while (id == 0 || alarm_set_find(set, id));
        set->last_id = id;
        slot = &set->items[set->count++];
    } else {
        slot = alarm_set_find(set, id);
        if (slot == NULL) {
            return 0;
        }
        if (set->snooze_id == id) {
            alarm_set_cancel_snooze(set);
        }
    }
    *slot = *a;
    slot->id = id;
    sort(set);
    return id;
}

uint8_t alarm_set_put_at(alarm_set_t *set, const alarm_t *a, const tz_posix_t *tz, int64_t now)
{
    const bool due_before = alarm_set_due(set, tz, now, NULL);
    const uint8_t id = alarm_set_put(set, a);
    if (id && !due_before && now > set->checked) {
        set->checked = now;
    }
    return id;
}

bool alarm_set_remove(alarm_set_t *set, uint8_t id)
{
    alarm_t *a = alarm_set_find(set, id);
    if (a == NULL) {
        return false;
    }
    const size_t i = (size_t)(a - set->items);
    memmove(a, a + 1, (set->count - i - 1) * sizeof *a);
    set->count--;
    if (set->snooze_id == id) {
        alarm_set_cancel_snooze(set);
    }
    return true;
}

int64_t alarm_next(const alarm_t *a, const tz_posix_t *tz, int64_t after)
{
    if (!a->enabled) {
        return ALARM_NEVER;
    }
    struct tm lt;
    tz_posix_localtime(tz, after, &lt);
    const int64_t day0 = days_from_civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
    // Day -1 covers an offset change between `after` and the alarm time.
    for (int k = -1; k <= 8; k++) {
        const int64_t d = day0 + k;
        if (a->days && !(a->days & (1u << weekday_of(d)))) {
            continue;
        }
        const int64_t u = local_to_utc(tz, d * 86400 + a->hour * 3600 + a->minute * 60);
        if (u > after) {
            return u;
        }
    }
    return ALARM_NEVER;
}

int64_t alarm_set_next(const alarm_set_t *set, const tz_posix_t *tz, int64_t after, alarm_due_t *out)
{
    alarm_due_t best = {.at = ALARM_NEVER};
    for (int i = 0; i < set->count; i++) {
        const int64_t t = alarm_next(&set->items[i], tz, after);
        if (t < best.at) {
            best = (alarm_due_t){.id = set->items[i].id, .snooze = false, .at = t};
        }
    }
    if (set->snooze_id && set->snooze_at > after && set->snooze_at < best.at) {
        best = (alarm_due_t){.id = set->snooze_id, .snooze = true, .at = set->snooze_at};
    }
    if (out) {
        *out = best;
    }
    return best.at;
}

static int64_t due_from(const alarm_set_t *set, int64_t now)
{
    return set->checked > now - ALARM_CATCHUP_S ? set->checked : now - ALARM_CATCHUP_S;
}

bool alarm_set_due(const alarm_set_t *set, const tz_posix_t *tz, int64_t now, alarm_due_t *out)
{
    alarm_due_t d;
    if (alarm_set_next(set, tz, due_from(set, now), &d) > now) {
        return false;
    }
    if (out) {
        *out = d;
    }
    return true;
}

void alarm_set_mark_rung(alarm_set_t *set, const tz_posix_t *tz, int64_t now)
{
    const int64_t from = due_from(set, now);
    for (int i = 0; i < set->count; i++) {
        alarm_t *a = &set->items[i];
        if (a->days == 0 && alarm_next(a, tz, from) <= now) {
            a->enabled = false;
        }
    }
    if (set->snooze_id && set->snooze_at <= now) {
        alarm_set_cancel_snooze(set);
    }
    if (now > set->checked) {
        set->checked = now;
    }
}

void alarm_set_snooze(alarm_set_t *set, uint8_t id, int64_t now)
{
    const alarm_t *a = alarm_set_find(set, id);
    set->snooze_id = id;
    set->snooze_at = now + (int64_t)(a ? a->snooze_min : ALARM_SNOOZE_DEFAULT) * 60;
}

void alarm_set_cancel_snooze(alarm_set_t *set)
{
    set->snooze_id = 0;
    set->snooze_at = 0;
}

void alarm_set_clock_changed(alarm_set_t *set, int64_t now)
{
    if (set->checked > now) {
        set->checked = now;
    }
}

void alarm_days_text(uint8_t days, char *buf, size_t len)
{
    static const char *const k_names[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    days &= ALARM_DAYS_ALL;
    if (days == 0) {
        snprintf(buf, len, "Once");
    } else if (days == ALARM_DAYS_ALL) {
        snprintf(buf, len, "Every day");
    } else if (days == ALARM_DAYS_WEEKDAYS) {
        snprintf(buf, len, "Mon-Fri");
    } else if (days == ALARM_DAYS_WEEKEND) {
        snprintf(buf, len, "Weekends");
    } else {
        size_t pos = 0;
        buf[0] = '\0';
        for (int k = 1; k <= 7; k++) { // Monday first
            const int d = k % 7;
            if ((days & (1u << d)) && pos < len) {
                pos += (size_t)snprintf(buf + pos, len - pos, "%s%s", pos ? " " : "", k_names[d]);
            }
        }
    }
}

// --- Persistence -------------------------------------------------------------------------

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
    }
    return ~c;
}

static void put_u64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static uint64_t get_u64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) {
        v = v << 8 | p[i];
    }
    return v;
}

size_t alarm_set_encode(const alarm_set_t *set, uint8_t *buf, size_t len)
{
    const size_t n = BLOB_HEADER + (size_t)set->count * BLOB_ITEM + 4;
    if (len < n) {
        return 0;
    }
    memset(buf, 0, n);
    buf[0] = BLOB_MAGIC0;
    buf[1] = BLOB_MAGIC1;
    buf[2] = BLOB_VERSION;
    buf[3] = set->count;
    buf[4] = set->last_id;
    buf[5] = set->snooze_id;
    put_u64(buf + 8, (uint64_t)set->snooze_at);
    put_u64(buf + 16, (uint64_t)set->checked);
    for (int i = 0; i < set->count; i++) {
        const alarm_t *a = &set->items[i];
        uint8_t *p = buf + BLOB_HEADER + i * BLOB_ITEM;
        p[0] = a->id;
        p[1] = a->enabled;
        p[2] = a->hour;
        p[3] = a->minute;
        p[4] = a->days;
        p[5] = a->snooze_min;
        memcpy(p + 6, a->label, sizeof a->label);
    }
    const uint32_t crc = crc32(buf, n - 4);
    for (int i = 0; i < 4; i++) {
        buf[n - 4 + i] = (uint8_t)(crc >> (8 * i));
    }
    return n;
}

bool alarm_set_decode(alarm_set_t *set, const uint8_t *buf, size_t len)
{
    if (len < BLOB_HEADER + 4 || buf[0] != BLOB_MAGIC0 || buf[1] != BLOB_MAGIC1 || buf[2] != BLOB_VERSION ||
        buf[3] > ALARM_MAX || len != BLOB_HEADER + (size_t)buf[3] * BLOB_ITEM + 4) {
        return false;
    }
    const uint32_t crc = (uint32_t)buf[len - 4] | (uint32_t)buf[len - 3] << 8 | (uint32_t)buf[len - 2] << 16 |
                         (uint32_t)buf[len - 1] << 24;
    if (crc32(buf, len - 4) != crc) {
        return false;
    }
    alarm_set_t s;
    alarm_set_init(&s);
    s.count = buf[3];
    s.last_id = buf[4];
    s.snooze_id = buf[5];
    s.snooze_at = (int64_t)get_u64(buf + 8);
    s.checked = (int64_t)get_u64(buf + 16);
    for (int i = 0; i < s.count; i++) {
        alarm_t *a = &s.items[i];
        const uint8_t *p = buf + BLOB_HEADER + i * BLOB_ITEM;
        a->id = p[0];
        a->enabled = p[1] != 0;
        a->hour = p[2];
        a->minute = p[3];
        a->days = p[4];
        a->snooze_min = p[5];
        memcpy(a->label, p + 6, sizeof a->label);
        if (a->id == 0 || !alarm_valid(a) || p[1] > 1) {
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (s.items[j].id == a->id) {
                return false;
            }
        }
    }
    if (s.snooze_id && !alarm_set_find(&s, s.snooze_id)) {
        alarm_set_cancel_snooze(&s);
    }
    sort(&s);
    *set = s;
    return true;
}
