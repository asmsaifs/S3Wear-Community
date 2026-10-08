// AOD burn-in shift pattern (watchfaces/wf_shift.c).
#include <gtest/gtest.h>

#include <cstdlib>
#include <set>
#include <utility>

extern "C" {
#include "wf_shift.h"
}

namespace {

std::pair<int, int> at(uint32_t minute)
{
    int8_t dx = 99;
    int8_t dy = 99;
    wf_aod_shift(minute, &dx, &dy);
    return {dx, dy};
}

TEST(WfShift, StaysWithinLimitAndEven)
{
    for (uint32_t m = 0; m < 3 * WF_AOD_SHIFT_PERIOD; m++) {
        const auto [dx, dy] = at(m);
        EXPECT_LE(std::abs(dx), WF_AOD_SHIFT_MAX_PX) << m;
        EXPECT_LE(std::abs(dy), WF_AOD_SHIFT_MAX_PX) << m;
        EXPECT_EQ(dx % 2, 0) << m;
        EXPECT_EQ(dy % 2, 0) << m;
    }
}

TEST(WfShift, OneSmallStepPerMinute)
{
    for (uint32_t m = 0; m < 2 * WF_AOD_SHIFT_PERIOD; m++) {
        const auto [x0, y0] = at(m);
        const auto [x1, y1] = at(m + 1);
        EXPECT_EQ(std::abs(x1 - x0) + std::abs(y1 - y0), 2) << "minute " << m;
    }
}

TEST(WfShift, VisitsEveryOffsetAndRepeats)
{
    std::set<std::pair<int, int>> seen;
    int sx = 0;
    int sy = 0;
    for (uint32_t m = 0; m < WF_AOD_SHIFT_PERIOD; m++) {
        const auto p = at(m);
        seen.insert(p);
        sx += p.first;
        sy += p.second;
        EXPECT_EQ(at(m + WF_AOD_SHIFT_PERIOD), p);
    }
    EXPECT_EQ(seen.size(), 25u);
    // Not exactly 0: the two ends of the snake are visited once, the rest twice.
    EXPECT_LE(std::abs(sx), 2 * WF_AOD_SHIFT_MAX_PX);
    EXPECT_LE(std::abs(sy), 2 * WF_AOD_SHIFT_MAX_PX);
}

TEST(WfShift, LargeMinuteCounts)
{
    const uint32_t m = 29823129u; // ~2026 in minutes since the epoch
    EXPECT_EQ(at(m), at(m % WF_AOD_SHIFT_PERIOD));
    const auto [dx, dy] = at(UINT32_MAX);
    EXPECT_LE(std::abs(dx), WF_AOD_SHIFT_MAX_PX);
    EXPECT_LE(std::abs(dy), WF_AOD_SHIFT_MAX_PX);
}

} // namespace
