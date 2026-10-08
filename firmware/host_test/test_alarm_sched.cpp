// alarm_sched: next occurrence (repeat days, zones, DST gap/overlap), due/catch-up,
// one-time alarms, snooze, edits, clock changes and the NVS blob.
#include <gtest/gtest.h>

#include <cstring>

#include "alarm_sched.h"

namespace {

// Unix seconds of a UTC date and time.
int64_t utc(int y, int mo, int d, int h = 0, int mi = 0, int s = 0)
{
    const int a = (14 - mo) / 12;
    const int64_t yy = y + 4800 - a;
    const int64_t mm = mo + 12 * a - 3;
    const int64_t jdn = d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
    return (jdn - 2440588) * 86400 + h * 3600 + mi * 60 + s;
}

tz_posix_t zone(const char *s)
{
    tz_posix_t tz;
    EXPECT_TRUE(tz_posix_parse(s, &tz)) << s;
    return tz;
}

alarm_t make(uint8_t h, uint8_t m, uint8_t days = 0)
{
    alarm_t a;
    alarm_default(&a, h, m);
    a.days = days;
    return a;
}

const char *PARIS = "CET-1CEST,M3.5.0,M10.5.0/3";
const char *NEW_YORK = "EST5EDT,M3.2.0,M11.1.0";

// 2026-10-03 is a Saturday.
const int64_t SAT_1009 = utc(2026, 10, 3, 10, 9);

} // namespace

TEST(AlarmNext, DailyInUtc)
{
    const tz_posix_t tz = zone("UTC0");
    const alarm_t a = make(7, 0, ALARM_DAYS_ALL);
    EXPECT_EQ(alarm_next(&a, &tz, SAT_1009), utc(2026, 10, 4, 7, 0));
    // Strictly after: at exactly 07:00 the next one is tomorrow.
    EXPECT_EQ(alarm_next(&a, &tz, utc(2026, 10, 4, 7, 0)), utc(2026, 10, 5, 7, 0));
    EXPECT_EQ(alarm_next(&a, &tz, utc(2026, 10, 4, 6, 59, 59)), utc(2026, 10, 4, 7, 0));
}

TEST(AlarmNext, OnceLaterTodayOrTomorrow)
{
    const tz_posix_t tz = zone("UTC0");
    const alarm_t later = make(10, 30);
    EXPECT_EQ(alarm_next(&later, &tz, SAT_1009), utc(2026, 10, 3, 10, 30));
    const alarm_t earlier = make(10, 9);
    EXPECT_EQ(alarm_next(&earlier, &tz, SAT_1009), utc(2026, 10, 4, 10, 9));
}

TEST(AlarmNext, RepeatDays)
{
    const tz_posix_t tz = zone("UTC0");
    const alarm_t weekdays = make(7, 0, ALARM_DAYS_WEEKDAYS);
    EXPECT_EQ(alarm_next(&weekdays, &tz, SAT_1009), utc(2026, 10, 5, 7, 0)); // Monday
    const alarm_t weekend = make(9, 0, ALARM_DAYS_WEEKEND);
    EXPECT_EQ(alarm_next(&weekend, &tz, SAT_1009), utc(2026, 10, 4, 9, 0)); // Sunday
    const alarm_t friday = make(8, 15, 1u << 5);
    EXPECT_EQ(alarm_next(&friday, &tz, SAT_1009), utc(2026, 10, 9, 8, 15));
    const alarm_t saturday_late = make(23, 0, 1u << 6);
    EXPECT_EQ(alarm_next(&saturday_late, &tz, SAT_1009), utc(2026, 10, 3, 23, 0));
}

TEST(AlarmNext, DisabledNever)
{
    const tz_posix_t tz = zone("UTC0");
    alarm_t a = make(7, 0, ALARM_DAYS_ALL);
    a.enabled = false;
    EXPECT_EQ(alarm_next(&a, &tz, SAT_1009), ALARM_NEVER);
}

TEST(AlarmNext, FixedOffsetZones)
{
    const tz_posix_t dhaka = zone("<+06>-6");
    const alarm_t a = make(7, 0, ALARM_DAYS_ALL);
    // Local 16:09 Saturday -> 07:00 Sunday local = 01:00 UTC.
    EXPECT_EQ(alarm_next(&a, &dhaka, SAT_1009), utc(2026, 10, 4, 1, 0));
    const tz_posix_t ny = zone("<-05>5");
    // Local 05:09 Saturday -> 07:00 Saturday local = 12:00 UTC.
    EXPECT_EQ(alarm_next(&a, &ny, SAT_1009), utc(2026, 10, 3, 12, 0));
    // The local day decides the weekday: Friday 23:30 UTC is Saturday morning in Dhaka.
    const alarm_t sat = make(7, 0, 1u << 6);
    EXPECT_EQ(alarm_next(&sat, &dhaka, utc(2026, 10, 2, 23, 30)), utc(2026, 10, 3, 1, 0));
}

TEST(AlarmNext, FollowsDst)
{
    const tz_posix_t tz = zone(PARIS);
    const alarm_t a = make(7, 0, ALARM_DAYS_ALL);
    // EU DST starts 2026-03-29 01:00 UTC.
    EXPECT_EQ(alarm_next(&a, &tz, utc(2026, 3, 28, 0, 0)), utc(2026, 3, 28, 6, 0)); // CET
    EXPECT_EQ(alarm_next(&a, &tz, utc(2026, 3, 28, 6, 0)), utc(2026, 3, 29, 5, 0)); // CEST
    // Ends 2026-10-25 01:00 UTC.
    EXPECT_EQ(alarm_next(&a, &tz, utc(2026, 10, 24, 12, 0)), utc(2026, 10, 25, 6, 0));
}

TEST(AlarmNext, DstGapRingsAfterTheGap)
{
    // 02:30 does not exist on 2026-03-29 in Paris: rings at 03:30 CEST = 01:30 UTC.
    const tz_posix_t paris = zone(PARIS);
    const alarm_t a = make(2, 30, ALARM_DAYS_ALL);
    EXPECT_EQ(alarm_next(&a, &paris, utc(2026, 3, 28, 12, 0)), utc(2026, 3, 29, 1, 30));
    // And on the next day normally (02:30 CEST = 00:30 UTC).
    EXPECT_EQ(alarm_next(&a, &paris, utc(2026, 3, 29, 1, 30)), utc(2026, 3, 30, 0, 30));
    // New York 2026-03-08: 02:30 -> 03:30 EDT = 07:30 UTC.
    const tz_posix_t ny = zone(NEW_YORK);
    EXPECT_EQ(alarm_next(&a, &ny, utc(2026, 3, 7, 12, 0)), utc(2026, 3, 8, 7, 30));
}

TEST(AlarmNext, DstOverlapRingsOnce)
{
    // 02:30 happens twice on 2026-10-25 in Paris (00:30 and 01:30 UTC): the first rings.
    const tz_posix_t tz = zone(PARIS);
    const alarm_t a = make(2, 30, ALARM_DAYS_ALL);
    const int64_t first = utc(2026, 10, 25, 0, 30);
    EXPECT_EQ(alarm_next(&a, &tz, utc(2026, 10, 24, 12, 0)), first);
    // Not again at the second 02:30, but on the next day (02:30 CET = 01:30 UTC).
    EXPECT_EQ(alarm_next(&a, &tz, first), utc(2026, 10, 26, 1, 30));
}

TEST(AlarmLocalToUtc, Conversion)
{
    const tz_posix_t tz = zone(PARIS);
    EXPECT_EQ(alarm_local_to_utc(&tz, 2026, 1, 15, 12, 0), utc(2026, 1, 15, 11, 0));
    EXPECT_EQ(alarm_local_to_utc(&tz, 2026, 7, 15, 12, 0), utc(2026, 7, 15, 10, 0));
    EXPECT_EQ(alarm_local_to_utc(&tz, 2028, 2, 29, 0, 0), utc(2028, 2, 28, 23, 0)); // leap day
}

TEST(AlarmSet, PutSortsAndAssignsIds)
{
    alarm_set_t set;
    alarm_set_init(&set);
    const alarm_t a = make(9, 0);
    const alarm_t b = make(6, 30);
    const alarm_t c = make(9, 0);
    const uint8_t ia = alarm_set_put(&set, &a);
    const uint8_t ib = alarm_set_put(&set, &b);
    const uint8_t ic = alarm_set_put(&set, &c);
    EXPECT_EQ(set.count, 3);
    EXPECT_NE(ia, 0);
    EXPECT_NE(ia, ib);
    EXPECT_NE(ib, ic);
    EXPECT_EQ(set.items[0].id, ib);
    EXPECT_EQ(set.items[1].id, ia);
    EXPECT_EQ(set.items[2].id, ic);

    alarm_t edit = *alarm_set_find(&set, ia);
    edit.hour = 5;
    EXPECT_EQ(alarm_set_put(&set, &edit), ia);
    EXPECT_EQ(set.items[0].id, ia);
    EXPECT_EQ(set.count, 3);

    alarm_t unknown = make(1, 0);
    unknown.id = 200;
    EXPECT_EQ(alarm_set_put(&set, &unknown), 0);
    alarm_t bad = make(24, 0);
    EXPECT_EQ(alarm_set_put(&set, &bad), 0);
    bad = make(7, 0);
    bad.snooze_min = 20;
    EXPECT_EQ(alarm_set_put(&set, &bad), 0);

    EXPECT_TRUE(alarm_set_remove(&set, ib));
    EXPECT_FALSE(alarm_set_remove(&set, ib));
    EXPECT_EQ(set.count, 2);
}

TEST(AlarmSet, FullAndIdWrap)
{
    alarm_set_t set;
    alarm_set_init(&set);
    set.last_id = 254;
    for (int i = 0; i < ALARM_MAX; i++) {
        const alarm_t a = make((uint8_t)i, 0);
        const uint8_t id = alarm_set_put(&set, &a);
        EXPECT_NE(id, 0) << i;
    }
    const alarm_t extra = make(23, 0);
    EXPECT_EQ(alarm_set_put(&set, &extra), 0);
    // ids stayed unique across the wrap at 255 -> 1.
    for (int i = 0; i < ALARM_MAX; i++) {
        for (int j = i + 1; j < ALARM_MAX; j++) {
            EXPECT_NE(set.items[i].id, set.items[j].id);
        }
        EXPECT_NE(set.items[i].id, 0);
    }
}

TEST(AlarmSet, DueAndOneTimeDisables)
{
    const tz_posix_t tz = zone("UTC0");
    alarm_set_t set;
    alarm_set_init(&set);
    const alarm_t once = make(10, 30);
    const uint8_t id = alarm_set_put_at(&set, &once, &tz, SAT_1009);
    const int64_t at = utc(2026, 10, 3, 10, 30);

    alarm_due_t d;
    EXPECT_FALSE(alarm_set_due(&set, &tz, at - 1, &d));
    ASSERT_TRUE(alarm_set_due(&set, &tz, at, &d));
    EXPECT_EQ(d.id, id);
    EXPECT_FALSE(d.snooze);
    EXPECT_EQ(d.at, at);
    // A late check still finds it.
    EXPECT_TRUE(alarm_set_due(&set, &tz, at + 3, &d));

    alarm_set_mark_rung(&set, &tz, at + 3);
    EXPECT_FALSE(alarm_set_find(&set, id)->enabled);
    EXPECT_FALSE(alarm_set_due(&set, &tz, at + 10, &d));
    EXPECT_EQ(alarm_set_next(&set, &tz, at + 10, nullptr), ALARM_NEVER);
}

TEST(AlarmSet, RepeatingStaysAndRingsNextDay)
{
    const tz_posix_t tz = zone("UTC0");
    alarm_set_t set;
    alarm_set_init(&set);
    const alarm_t daily = make(7, 0, ALARM_DAYS_ALL);
    const uint8_t id = alarm_set_put_at(&set, &daily, &tz, SAT_1009);
    const int64_t at = utc(2026, 10, 4, 7, 0);
    ASSERT_TRUE(alarm_set_due(&set, &tz, at, nullptr));
    alarm_set_mark_rung(&set, &tz, at);
    EXPECT_TRUE(alarm_set_find(&set, id)->enabled);
    alarm_due_t d;
    EXPECT_EQ(alarm_set_next(&set, &tz, set.checked, &d), utc(2026, 10, 5, 7, 0));
}

TEST(AlarmSet, TwoAtTheSameMinuteRingOnce)
{
    const tz_posix_t tz = zone("UTC0");
    alarm_set_t set;
    alarm_set_init(&set);
    const alarm_t a = make(10, 30);
    const alarm_t b = make(10, 30);
    const uint8_t ia = alarm_set_put_at(&set, &a, &tz, SAT_1009);
    const uint8_t ib = alarm_set_put_at(&set, &b, &tz, SAT_1009);
    const int64_t at = utc(2026, 10, 3, 10, 30);
    ASSERT_TRUE(alarm_set_due(&set, &tz, at, nullptr));
    alarm_set_mark_rung(&set, &tz, at);
    EXPECT_FALSE(alarm_set_find(&set, ia)->enabled);
    EXPECT_FALSE(alarm_set_find(&set, ib)->enabled);
    EXPECT_FALSE(alarm_set_due(&set, &tz, at + 1, nullptr));
}

TEST(AlarmSet, CatchUpWindow)
{
    const tz_posix_t tz = zone("UTC0");
    alarm_set_t set;
    alarm_set_init(&set);
    const alarm_t daily = make(7, 0, ALARM_DAYS_ALL);
    alarm_set_put(&set, &daily);
    set.checked = utc(2026, 10, 1, 0, 0); // e.g. off since then
    const int64_t at = utc(2026, 10, 4, 7, 0);
    // A boot 4 min late still rings (deep-sleep wake, slow boot).
    alarm_due_t d;
    ASSERT_TRUE(alarm_set_due(&set, &tz, at + 4 * 60, &d));
    EXPECT_EQ(d.at, at);
    // More than ALARM_CATCHUP_S late: skipped (and not yesterday's either).
    EXPECT_FALSE(alarm_set_due(&set, &tz, at + ALARM_CATCHUP_S + 1, &d));
}

TEST(AlarmSet, EditedAlarmForThisMinuteDoesNotRingAtOnce)
{
    const tz_posix_t tz = zone("UTC0");
    alarm_set_t set;
    alarm_set_init(&set);
    const int64_t now = utc(2026, 10, 3, 10, 9, 30);
    const alarm_t a = make(10, 9);
    const uint8_t id = alarm_set_put_at(&set, &a, &tz, now);
    alarm_due_t d;
    EXPECT_FALSE(alarm_set_due(&set, &tz, now, &d));
    EXPECT_EQ(alarm_set_next(&set, &tz, now, &d), utc(2026, 10, 4, 10, 9));
    EXPECT_EQ(d.id, id);
    // The next minute still rings.
    const alarm_t b = make(10, 10);
    alarm_set_put_at(&set, &b, &tz, now + 1);
    EXPECT_TRUE(alarm_set_due(&set, &tz, utc(2026, 10, 3, 10, 10), nullptr));
}

TEST(AlarmSet, EditWhileDueKeepsTheDueAlarm)
{
    const tz_posix_t tz = zone("UTC0");
    alarm_set_t set;
    alarm_set_init(&set);
    const alarm_t a = make(10, 30);
    alarm_set_put_at(&set, &a, &tz, SAT_1009);
    const int64_t at = utc(2026, 10, 3, 10, 30);
    // An edit lands between the timer firing and the service handling it.
    const alarm_t b = make(12, 0);
    alarm_set_put_at(&set, &b, &tz, at + 1);
    EXPECT_TRUE(alarm_set_due(&set, &tz, at + 1, nullptr));
}

TEST(AlarmSet, Snooze)
{
    const tz_posix_t tz = zone("UTC0");
    alarm_set_t set;
    alarm_set_init(&set);
    alarm_t a = make(7, 0);
    a.snooze_min = 5;
    const uint8_t id = alarm_set_put_at(&set, &a, &tz, SAT_1009);
    const int64_t at = utc(2026, 10, 4, 7, 0);
    ASSERT_TRUE(alarm_set_due(&set, &tz, at, nullptr));
    alarm_set_mark_rung(&set, &tz, at);
    EXPECT_FALSE(alarm_set_find(&set, id)->enabled); // once
    alarm_set_snooze(&set, id, at + 20);

    alarm_due_t d;
    EXPECT_EQ(alarm_set_next(&set, &tz, at + 20, &d), at + 20 + 5 * 60);
    EXPECT_TRUE(d.snooze);
    EXPECT_EQ(d.id, id);
    EXPECT_FALSE(alarm_set_due(&set, &tz, at + 5 * 60, &d));
    ASSERT_TRUE(alarm_set_due(&set, &tz, at + 20 + 5 * 60, &d));
    EXPECT_TRUE(d.snooze);
    alarm_set_mark_rung(&set, &tz, at + 20 + 5 * 60);
    EXPECT_EQ(set.snooze_id, 0);
    EXPECT_EQ(alarm_set_next(&set, &tz, set.checked, nullptr), ALARM_NEVER);

    // Editing or removing the alarm cancels its snooze.
    alarm_set_snooze(&set, id, at + 1000);
    alarm_t e = *alarm_set_find(&set, id);
    alarm_set_put(&set, &e);
    EXPECT_EQ(set.snooze_id, 0);
    alarm_set_snooze(&set, id, at + 1000);
    alarm_set_remove(&set, id);
    EXPECT_EQ(set.snooze_id, 0);
}

TEST(AlarmSet, ClockSetBack)
{
    const tz_posix_t tz = zone("UTC0");
    alarm_set_t set;
    alarm_set_init(&set);
    const alarm_t daily = make(10, 30, ALARM_DAYS_ALL);
    alarm_set_put(&set, &daily);
    set.checked = utc(2026, 10, 3, 11, 0);
    // The clock goes back an hour: 10:30 today must ring again.
    alarm_set_clock_changed(&set, SAT_1009);
    EXPECT_EQ(set.checked, SAT_1009);
    EXPECT_EQ(alarm_set_next(&set, &tz, set.checked, nullptr), utc(2026, 10, 3, 10, 30));
    // Forward changes leave checked alone.
    alarm_set_clock_changed(&set, utc(2026, 10, 3, 12, 0));
    EXPECT_EQ(set.checked, SAT_1009);
}

TEST(AlarmDays, Text)
{
    char buf[32];
    alarm_days_text(0, buf, sizeof buf);
    EXPECT_STREQ(buf, "Once");
    alarm_days_text(ALARM_DAYS_ALL, buf, sizeof buf);
    EXPECT_STREQ(buf, "Every day");
    alarm_days_text(ALARM_DAYS_WEEKDAYS, buf, sizeof buf);
    EXPECT_STREQ(buf, "Mon-Fri");
    alarm_days_text(ALARM_DAYS_WEEKEND, buf, sizeof buf);
    EXPECT_STREQ(buf, "Weekends");
    alarm_days_text((1u << 1) | (1u << 3) | (1u << 5), buf, sizeof buf);
    EXPECT_STREQ(buf, "Mon Wed Fri");
    alarm_days_text((1u << 0) | (1u << 2), buf, sizeof buf);
    EXPECT_STREQ(buf, "Tue Sun");
}

TEST(AlarmBlob, RoundTrip)
{
    alarm_set_t set;
    alarm_set_init(&set);
    alarm_t a = make(6, 45, ALARM_DAYS_WEEKDAYS);
    std::strcpy(a.label, "Gym");
    a.snooze_min = 15;
    const uint8_t ia = alarm_set_put(&set, &a);
    alarm_t b = make(22, 0);
    b.enabled = false;
    alarm_set_put(&set, &b);
    alarm_set_snooze(&set, ia, 1791022140);
    set.checked = 1791022000;

    uint8_t buf[ALARM_BLOB_MAX];
    const size_t n = alarm_set_encode(&set, buf, sizeof buf);
    ASSERT_GT(n, 0u);
    alarm_set_t out;
    alarm_set_init(&out);
    ASSERT_TRUE(alarm_set_decode(&out, buf, n));
    EXPECT_EQ(std::memcmp(&out, &set, sizeof set), 0);

    // A full set fits ALARM_BLOB_MAX.
    alarm_set_t full;
    alarm_set_init(&full);
    for (int i = 0; i < ALARM_MAX; i++) {
        alarm_t x = make((uint8_t)i, 0);
        std::memset(x.label, 'x', ALARM_LABEL_MAX);
        alarm_set_put(&full, &x);
    }
    EXPECT_EQ(alarm_set_encode(&full, buf, sizeof buf), (size_t)ALARM_BLOB_MAX);
    EXPECT_EQ(alarm_set_encode(&full, buf, ALARM_BLOB_MAX - 1), 0u);
}

TEST(AlarmBlob, RejectsDamage)
{
    alarm_set_t set;
    alarm_set_init(&set);
    const alarm_t a = make(7, 0);
    alarm_set_put(&set, &a);
    uint8_t buf[ALARM_BLOB_MAX];
    const size_t n = alarm_set_encode(&set, buf, sizeof buf);
    alarm_set_t out;
    alarm_set_init(&out);
    out.count = 7; // must stay untouched on failure

    uint8_t bad[ALARM_BLOB_MAX];
    std::memcpy(bad, buf, n);
    bad[30] ^= 1; // label byte -> CRC mismatch
    EXPECT_FALSE(alarm_set_decode(&out, bad, n));
    EXPECT_FALSE(alarm_set_decode(&out, buf, n - 1)); // length
    std::memcpy(bad, buf, n);
    bad[2] = 9; // version
    EXPECT_FALSE(alarm_set_decode(&out, bad, n));
    EXPECT_EQ(out.count, 7);

    // Valid CRC but an invalid alarm (hour 25).
    alarm_set_t evil = set;
    evil.items[0].hour = 25;
    const size_t m = alarm_set_encode(&evil, bad, sizeof bad);
    EXPECT_FALSE(alarm_set_decode(&out, bad, m));
    EXPECT_EQ(out.count, 7);
}
