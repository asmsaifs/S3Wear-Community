// watch_only: minute shown by a tick, deep-sleep time to the next tick and to the
// alarm wake, and whether a timer wake is a tick or the alarm.
#include <gtest/gtest.h>

#include "watch_only.h"

namespace {

constexpr int64_t MIN_MS = 60000;
constexpr int64_t T0 = 1791022140LL * 1000; // 2026-10-03 10:09:00 UTC, a minute start

} // namespace

TEST(WatchOnly, MinuteShown)
{
    EXPECT_EQ(watch_only_minute(T0), T0 / MIN_MS);
    EXPECT_EQ(watch_only_minute(T0 + 1500), T0 / MIN_MS);
    EXPECT_EQ(watch_only_minute(T0 + 56999), T0 / MIN_MS);
    // Within WATCH_ONLY_EARLY_MS of the next minute: show it already.
    EXPECT_EQ(watch_only_minute(T0 + 57000), T0 / MIN_MS + 1);
    EXPECT_EQ(watch_only_minute(T0 + 59999), T0 / MIN_MS + 1);
}

TEST(WatchOnly, TickAimsAfterNextMinute)
{
    EXPECT_EQ(watch_only_sleep_us(T0 + 1500, 0, true), (uint64_t)60000 * 1000);
    EXPECT_EQ(watch_only_sleep_us(T0 + 30000, 0, true), (uint64_t)31500 * 1000);
    // An early tick (showing the next minute) sleeps until the one after it.
    EXPECT_EQ(watch_only_sleep_us(T0 + 58000, 0, true), (uint64_t)63500 * 1000);
}

TEST(WatchOnly, NoTimerWithoutTicksOrAlarm)
{
    EXPECT_EQ(watch_only_sleep_us(T0, 0, false), 0u);
}

TEST(WatchOnly, AlarmWakeEarlyByLead)
{
    // Alarm in 1000 s without ticks: lead 20 s (2 %).
    EXPECT_EQ(watch_only_sleep_us(T0, T0 / 1000 + 1000, false), (uint64_t)980 * 1000000);
    // Alarm in 100 s: lead at least 5 s.
    EXPECT_EQ(watch_only_sleep_us(T0, T0 / 1000 + 100, false), (uint64_t)95 * 1000000);
    // Inside the lead: at once.
    EXPECT_EQ(watch_only_sleep_us(T0, T0 / 1000 + 3, false), 1000u);
}

TEST(WatchOnly, EarlierOfTickAndAlarm)
{
    // Tick in 61.5 s, alarm wake in 25 s: the alarm wins.
    EXPECT_EQ(watch_only_sleep_us(T0, T0 / 1000 + 30, true), (uint64_t)25 * 1000000);
    // Alarm far away: the tick wins.
    EXPECT_EQ(watch_only_sleep_us(T0, T0 / 1000 + 3600, true), (uint64_t)61500 * 1000);
}

TEST(WatchOnly, WakeKind)
{
    const int64_t now = T0 / 1000;
    EXPECT_EQ(watch_only_wake_kind(now, 0), WATCH_ONLY_WAKE_TICK);
    EXPECT_EQ(watch_only_wake_kind(now, now + 71), WATCH_ONLY_WAKE_TICK);
    EXPECT_EQ(watch_only_wake_kind(now, now + 70), WATCH_ONLY_WAKE_ALARM);
    EXPECT_EQ(watch_only_wake_kind(now, now - 30), WATCH_ONLY_WAKE_ALARM); // missed by a slow wake
}

TEST(WatchOnly, TicksReachTheAlarmBootWindow)
{
    // Starting anywhere, following the ticks never jumps past the alarm boot window
    // even with the RC clock 3 % fast or slow.
    const int64_t alarm = T0 / 1000 + 7 * 60 + 13;
    for (int drift : {-3, 0, 3}) {
        int64_t now = T0 + 12345;
        bool booted = false;
        for (int i = 0; i < 20 && !booted; i++) {
            if (watch_only_wake_kind(now / 1000, alarm) == WATCH_ONLY_WAKE_ALARM) {
                EXPECT_LE(now / 1000, alarm - 5) << "drift " << drift;
                booted = true;
                break;
            }
            const int64_t us = (int64_t)watch_only_sleep_us(now, alarm, true);
            now += us / 1000 * (100 + drift) / 100;
        }
        EXPECT_TRUE(booted) << "drift " << drift;
    }
}
