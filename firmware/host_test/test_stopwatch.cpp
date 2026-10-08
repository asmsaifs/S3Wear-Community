// stopwatch (start/stop/laps/overflow/format) and the world clock city table.
#include <gtest/gtest.h>

#include <cstring>

#include "stopwatch.h"
#include "tz_posix.h"
#include "world_clock.h"

TEST(Stopwatch, StartStopResume)
{
    stopwatch_t sw;
    stopwatch_reset(&sw);
    EXPECT_EQ(stopwatch_elapsed(&sw, 5000), 0u);
    stopwatch_start(&sw, 1000);
    EXPECT_EQ(stopwatch_elapsed(&sw, 3500), 2500u);
    stopwatch_stop(&sw, 4000);
    EXPECT_EQ(stopwatch_elapsed(&sw, 9000), 3000u);
    EXPECT_FALSE(stopwatch_lap(&sw, 9000)); // stopped
    stopwatch_start(&sw, 10000);
    EXPECT_EQ(stopwatch_elapsed(&sw, 11000), 4000u);
}

TEST(Stopwatch, Laps)
{
    stopwatch_t sw;
    stopwatch_reset(&sw);
    stopwatch_start(&sw, 0);
    EXPECT_TRUE(stopwatch_lap(&sw, 10000));
    EXPECT_TRUE(stopwatch_lap(&sw, 25000));
    ASSERT_EQ(sw.lap_count, 2);
    EXPECT_EQ(stopwatch_lap_ms(&sw, 0), 10000u);
    EXPECT_EQ(stopwatch_lap_ms(&sw, 1), 15000u);
    EXPECT_EQ(stopwatch_lap_number(&sw, 1), 2);
}

TEST(Stopwatch, LapOverflowKeepsNewest)
{
    stopwatch_t sw;
    stopwatch_reset(&sw);
    stopwatch_start(&sw, 0);
    for (int i = 1; i <= STOPWATCH_LAPS_MAX + 5; i++) {
        stopwatch_lap(&sw, (uint32_t)i * 1000u * i); // lap i ends at i*i s
    }
    EXPECT_EQ(sw.lap_count, STOPWATCH_LAPS_MAX);
    EXPECT_EQ(sw.lap_total, STOPWATCH_LAPS_MAX + 5);
    const size_t last = STOPWATCH_LAPS_MAX - 1;
    const int n = STOPWATCH_LAPS_MAX + 5;
    EXPECT_EQ(stopwatch_lap_number(&sw, last), n);
    EXPECT_EQ(stopwatch_lap_ms(&sw, last), (uint32_t)(n * n - (n - 1) * (n - 1)) * 1000u);
    EXPECT_EQ(stopwatch_lap_number(&sw, 0), 6);
    EXPECT_EQ(stopwatch_lap_ms(&sw, 0), 0u); // its start mark dropped out
    EXPECT_EQ(stopwatch_lap_ms(&sw, 1), (uint32_t)(7 * 7 - 6 * 6) * 1000u);
}

TEST(Stopwatch, Format)
{
    char buf[16];
    uint8_t cs = 0;
    stopwatch_format(0, buf, sizeof buf, &cs);
    EXPECT_STREQ(buf, "00:00");
    EXPECT_EQ(cs, 0);
    stopwatch_format(83456, buf, sizeof buf, &cs);
    EXPECT_STREQ(buf, "01:23");
    EXPECT_EQ(cs, 45);
    stopwatch_format(3599999, buf, sizeof buf, &cs);
    EXPECT_STREQ(buf, "59:59");
    EXPECT_EQ(cs, 99);
    stopwatch_format(3600000 + 61000, buf, sizeof buf, nullptr);
    EXPECT_STREQ(buf, "1:01:01");
}

TEST(WorldClock, EveryCityParses)
{
    ASSERT_GT(world_city_count(), 20u);
    for (size_t i = 0; i < world_city_count(); i++) {
        const world_city_t *c = world_city_at(i);
        tz_posix_t tz;
        EXPECT_TRUE(tz_posix_parse(c->tz, &tz)) << c->id << ": " << c->tz;
        EXPECT_LE(std::strlen(c->label), 7u) << c->id;
        EXPECT_EQ(world_city_find(c->id), c);
    }
    EXPECT_EQ(world_city_at(world_city_count()), nullptr);
}

TEST(WorldClock, Offsets)
{
    // 2026-07-01 12:00 UTC and 2026-01-01 12:00 UTC.
    const int64_t jul = 1782907200;
    const int64_t jan = 1767268800;
    EXPECT_EQ(world_city_offset(world_city_find("new_york"), jul), -4 * 3600);
    EXPECT_EQ(world_city_offset(world_city_find("new_york"), jan), -5 * 3600);
    EXPECT_EQ(world_city_offset(world_city_find("sydney"), jul), 10 * 3600);
    EXPECT_EQ(world_city_offset(world_city_find("sydney"), jan), 11 * 3600);
    EXPECT_EQ(world_city_offset(world_city_find("kathmandu"), jan), 5 * 3600 + 45 * 60);
    EXPECT_EQ(world_city_offset(world_city_find("delhi"), jan), 5 * 3600 + 30 * 60);
}

TEST(WorldClock, ParseAndJoin)
{
    const world_city_t *c[WORLD_CLOCK_MAX];
    EXPECT_EQ(world_clock_parse("", c, WORLD_CLOCK_MAX), 0u);
    EXPECT_EQ(world_clock_parse(nullptr, c, WORLD_CLOCK_MAX), 0u);
    ASSERT_EQ(world_clock_parse("tokyo,nowhere,london,tokyo,,paris", c, WORLD_CLOCK_MAX), 3u);
    EXPECT_STREQ(c[0]->id, "tokyo");
    EXPECT_STREQ(c[1]->id, "london");
    EXPECT_STREQ(c[2]->id, "paris");
    char buf[96];
    world_clock_join(c, 3, buf, sizeof buf);
    EXPECT_STREQ(buf, "tokyo,london,paris");
    EXPECT_EQ(world_clock_parse("tokyo,london,paris,delhi,dhaka,cairo,dubai", c, WORLD_CLOCK_MAX),
              (size_t)WORLD_CLOCK_MAX);
}

TEST(WorldClock, Relative)
{
    char buf[40];
    const int64_t t = 1791022140; // 2026-10-03 10:09 UTC
    world_clock_relative(9 * 3600, 0, t, buf, sizeof buf);
    EXPECT_STREQ(buf, "Today, +9 h"); // 19:09
    world_clock_relative(14 * 3600, 0, t, buf, sizeof buf);
    EXPECT_STREQ(buf, "Tomorrow, +14 h");
    world_clock_relative(-11 * 3600, 0, t, buf, sizeof buf);
    EXPECT_STREQ(buf, "Yesterday, -11 h");
    world_clock_relative(5 * 3600 + 30 * 60, 6 * 3600, t, buf, sizeof buf);
    EXPECT_STREQ(buf, "Today, -0:30 h");
    world_clock_relative(3600, 3600, t, buf, sizeof buf);
    EXPECT_STREQ(buf, "Today, same time");
}
