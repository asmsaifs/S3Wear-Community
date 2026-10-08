// battery_hist: 24 h history ring (slots, gaps, wrap) and the time-to-full /
// time-left estimate (anchor, minimum data, direction changes, CV taper).
#include <gtest/gtest.h>

#include "battery_hist.h"

namespace {

constexpr uint32_t SLOT = BATTERY_HIST_SLOT_S;
constexpr int LAST = BATTERY_HIST_SLOTS - 1;

battery_hist_t fresh()
{
    battery_hist_t h;
    battery_hist_init(&h);
    return h;
}

} // namespace

TEST(BatteryHist, EmptyHasNoReadings)
{
    battery_hist_t h = fresh();
    uint8_t out[BATTERY_HIST_SLOTS];
    battery_hist_get(&h, 1000, out);
    for (uint8_t v : out) {
        EXPECT_EQ(v, BATTERY_HIST_NONE);
    }
}

TEST(BatteryHist, LastReadingInSlotWins)
{
    battery_hist_t h = fresh();
    battery_hist_add(&h, 100, 80, false);
    battery_hist_add(&h, 160, 79, false);
    uint8_t out[BATTERY_HIST_SLOTS];
    battery_hist_get(&h, 200, out);
    EXPECT_EQ(out[LAST], 79);
    EXPECT_EQ(out[LAST - 1], BATTERY_HIST_NONE);
}

TEST(BatteryHist, SlotsMoveWithTimeAndChargingFlag)
{
    battery_hist_t h = fresh();
    battery_hist_add(&h, 0, 50, false);
    battery_hist_add(&h, SLOT, 49, false);
    battery_hist_add(&h, 2 * SLOT + 5, 52, true);
    uint8_t out[BATTERY_HIST_SLOTS];
    battery_hist_get(&h, 2 * SLOT + 10, out);
    EXPECT_EQ(out[LAST - 2], 50);
    EXPECT_EQ(out[LAST - 1], 49);
    EXPECT_EQ(out[LAST], 52 | BATTERY_HIST_CHARGING);
    EXPECT_EQ(BATTERY_HIST_PCT(out[LAST]), 52);
    // Reading later without new data: the old slots slide left, now has none.
    battery_hist_get(&h, 3 * SLOT, out);
    EXPECT_EQ(out[LAST - 1], 52 | BATTERY_HIST_CHARGING);
    EXPECT_EQ(out[LAST], BATTERY_HIST_NONE);
}

TEST(BatteryHist, GapsHaveNoReading)
{
    battery_hist_t h = fresh();
    battery_hist_add(&h, 0, 70, false);
    battery_hist_add(&h, 3 * SLOT, 60, false);
    uint8_t out[BATTERY_HIST_SLOTS];
    battery_hist_get(&h, 3 * SLOT, out);
    EXPECT_EQ(out[LAST - 3], 70);
    EXPECT_EQ(out[LAST - 2], BATTERY_HIST_NONE);
    EXPECT_EQ(out[LAST - 1], BATTERY_HIST_NONE);
    EXPECT_EQ(out[LAST], 60);
}

TEST(BatteryHist, OlderThanADayDropsOut)
{
    battery_hist_t h = fresh();
    for (uint32_t i = 0; i < 100; i++) {
        battery_hist_add(&h, i * SLOT, (int)(100 - i), false);
    }
    uint8_t out[BATTERY_HIST_SLOTS];
    battery_hist_get(&h, 99 * SLOT, out);
    EXPECT_EQ(out[LAST], 1);
    EXPECT_EQ(out[0], 96); // slot 4 = 100 - 4
    // A gap longer than the whole ring clears it.
    battery_hist_add(&h, 300 * SLOT, 40, false);
    battery_hist_get(&h, 300 * SLOT, out);
    EXPECT_EQ(out[LAST], 40);
    EXPECT_EQ(out[LAST - 1], BATTERY_HIST_NONE);
    EXPECT_EQ(out[0], BATTERY_HIST_NONE);
}

TEST(BatteryHist, NoBatteryIsNoReading)
{
    battery_hist_t h = fresh();
    battery_hist_add(&h, 0, -1, false);
    uint8_t out[BATTERY_HIST_SLOTS];
    battery_hist_get(&h, 0, out);
    EXPECT_EQ(out[LAST], BATTERY_HIST_NONE);
    EXPECT_EQ(battery_hist_minutes(&h, 0, -1, false), -1);
}

TEST(BatteryEst, UnknownUntilEnoughData)
{
    battery_hist_t h = fresh();
    battery_hist_add(&h, 0, 80, false);
    EXPECT_EQ(battery_hist_minutes(&h, 0, 80, false), -1);
    battery_hist_add(&h, 3000, 79, false); // 1 %: not enough
    EXPECT_EQ(battery_hist_minutes(&h, 3000, 79, false), -1);
    battery_hist_t q = fresh();
    battery_hist_add(&q, 0, 80, false);
    battery_hist_add(&q, 300, 78, false); // 2 % but only 5 min
    EXPECT_EQ(battery_hist_minutes(&q, 300, 78, false), -1);
}

TEST(BatteryEst, DischargeRate)
{
    battery_hist_t h = fresh();
    battery_hist_add(&h, 0, 80, false);
    battery_hist_add(&h, 7200, 78, false); // 1 % per hour
    EXPECT_EQ(battery_hist_minutes(&h, 7200, 78, false), 78 * 60);
    // Later readings average over the whole segment.
    battery_hist_add(&h, 10800, 76, false); // 4 % in 3 h
    EXPECT_EQ(battery_hist_minutes(&h, 10800, 76, false), 76 * 45);
}

TEST(BatteryEst, ChargeRateWithTaper)
{
    battery_hist_t h = fresh();
    battery_hist_add(&h, 0, 40, true);
    battery_hist_add(&h, 1200, 50, true); // 10 % in 20 min = 2 min per %
    // 50 -> 100 = 50 %, plus 20 % (80..100 count double) = 70 % -> 140 min
    EXPECT_EQ(battery_hist_minutes(&h, 1200, 50, true), 140);
    battery_hist_add(&h, 5400, 90, true); // 50 % in 90 min
    // 10 % + 10 % = 20 % at 1.8 min -> 36 min
    EXPECT_EQ(battery_hist_minutes(&h, 5400, 90, true), 36);
    battery_hist_add(&h, 7000, 100, true);
    EXPECT_EQ(battery_hist_minutes(&h, 7000, 100, true), 0);
}

TEST(BatteryEst, DirectionChangeRestarts)
{
    battery_hist_t h = fresh();
    battery_hist_add(&h, 0, 80, false);
    battery_hist_add(&h, 7200, 70, false);
    ASSERT_GT(battery_hist_minutes(&h, 7200, 70, false), 0);
    battery_hist_add(&h, 7300, 70, true); // plugged in
    EXPECT_EQ(battery_hist_minutes(&h, 7300, 70, true), -1);
    // Asking with the other direction than the data: unknown.
    EXPECT_EQ(battery_hist_minutes(&h, 7300, 70, false), -1);
    battery_hist_add(&h, 7400, 69, true); // gauge went down while charging: new anchor
    battery_hist_add(&h, 8000, 71, true);
    EXPECT_EQ(battery_hist_minutes(&h, 8000, 71, true), (29 + 20) * 600 / 2 / 60);
}
