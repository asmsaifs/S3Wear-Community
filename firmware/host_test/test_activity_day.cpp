// activity_day: distance, kcal, active minutes, goals and the midnight rollover.
#include <gtest/gtest.h>

#include "activity_day.h"

namespace {

activity_profile_t profile()
{
    activity_profile_t p{};
    p.height_cm = 175;
    p.weight_kg = 70;
    p.sex = ACT_SEX_MALE;
    p.birth_year = 1990;
    p.step_goal = 1000;
    p.active_goal_min = 3;
    return p;
}

} // namespace

TEST(ActivityDay, StrideAndBmr)
{
    activity_profile_t p = profile();
    EXPECT_EQ(activity_stride_cm(&p, 100), 72u);      // 175 × 0.415
    EXPECT_EQ(activity_stride_cm(&p, 160), 72u * 13 / 10); // running
    p.sex = ACT_SEX_FEMALE;
    EXPECT_EQ(activity_stride_cm(&p, 100), 72u);      // 175 × 0.413 = 72.3
    p.sex = ACT_SEX_MALE;
    // 10 × 70 + 6.25 × 175 − 5 × 36 + 5 = 1618.75
    EXPECT_EQ(activity_bmr_kcal(&p, 2026), 1618u);
    p.sex = ACT_SEX_FEMALE;
    EXPECT_EQ(activity_bmr_kcal(&p, 2026), 1452u); // − 161
}

TEST(ActivityDay, MinuteKcalRisesWithCadence)
{
    const activity_profile_t p = profile();
    uint32_t last = 0;
    for (uint32_t spm = 1; spm < 200; spm++) {
        const uint32_t k = activity_minute_mkcal(&p, spm);
        EXPECT_GE(k, last) << spm;
        last = k;
    }
    EXPECT_EQ(activity_minute_mkcal(&p, 100), 2916u); // (3.5 − 1) × 70 / 60 kcal
}

TEST(ActivityDay, TotalsActiveMinutesGoals)
{
    const activity_profile_t p = profile();
    activity_day_t d;
    activity_day_init(&d, 20261005);
    activity_summary_t ended;
    // Minute 600: 100 steps in 4 batches.
    unsigned r = 0;
    for (int i = 0; i < 4; i++) {
        r |= activity_day_add(&d, &p, 20261005, 600, 25, &ended);
    }
    EXPECT_EQ(r, 0u);
    EXPECT_EQ(d.t.steps, 100u);
    EXPECT_EQ(d.t.active_min, 1);
    EXPECT_EQ(d.t.distance_cm, 100u * 72);
    EXPECT_EQ(d.t.active_mkcal, 2916u); // the minute at its final cadence, not the sum of parts
    // Minute 601: 59 steps is not active; 602, 603: active -> goal of 3.
    EXPECT_EQ(activity_day_add(&d, &p, 20261005, 601, 59, &ended), 0u);
    EXPECT_EQ(d.t.active_min, 1);
    EXPECT_EQ(activity_day_add(&d, &p, 20261005, 602, 120, &ended), 0u);
    EXPECT_EQ(activity_day_add(&d, &p, 20261005, 603, 120, &ended), (unsigned)ACT_ACTIVE_GOAL);
    EXPECT_EQ(d.t.active_min, 3);
    // Steps goal 1000 once.
    EXPECT_EQ(activity_day_add(&d, &p, 20261005, 700, 601, &ended), (unsigned)ACT_STEP_GOAL);
    EXPECT_EQ(activity_day_add(&d, &p, 20261005, 701, 50, &ended), 0u);

    activity_summary_t s;
    activity_day_summary(&d, &p, 719, &s); // 12:00: half the BMR
    EXPECT_EQ(s.steps, 1050u);
    EXPECT_EQ(s.kcal, s.active_kcal + 1618 / 2);
    EXPECT_EQ(s.distance_m, d.t.distance_cm / 100);
}

TEST(ActivityDay, MidnightRollover)
{
    const activity_profile_t p = profile();
    activity_day_t d;
    activity_day_init(&d, 20261005);
    activity_summary_t ended{};
    activity_day_add(&d, &p, 20261005, 1439, 80, &ended);
    // A batch-less check after midnight ends the day.
    EXPECT_EQ(activity_day_add(&d, &p, 20261006, 0, 0, &ended), (unsigned)ACT_DAY_ENDED);
    EXPECT_EQ(ended.day, 20261005u);
    EXPECT_EQ(ended.steps, 80u);
    EXPECT_EQ(ended.kcal, ended.active_kcal + 1618); // whole day's BMR
    EXPECT_EQ(d.t.day, 20261006u);
    EXPECT_EQ(d.t.steps, 0u);
    EXPECT_EQ(d.minute_steps[1439], 0);
    activity_day_add(&d, &p, 20261006, 1, 10, &ended);
    EXPECT_EQ(d.t.steps, 10u);
    // The clock goes back a day: keep counting today.
    EXPECT_EQ(activity_day_add(&d, &p, 20261005, 1438, 10, &ended), 0u);
    EXPECT_EQ(d.t.day, 20261006u);
    EXPECT_EQ(d.t.steps, 20u);
    // Goals are per day.
    EXPECT_FALSE(d.t.step_goal_hit);
}

TEST(ActivityDay, UnknownDayThenClockSet)
{
    const activity_profile_t p = profile();
    activity_day_t d;
    activity_day_init(&d, 0);
    activity_summary_t ended;
    activity_day_add(&d, &p, 0, 5, 30, &ended);
    EXPECT_EQ(activity_day_add(&d, &p, 20261005, 600, 0, &ended), 0u); // adopted, not ended
    EXPECT_EQ(d.t.day, 20261005u);
    EXPECT_EQ(d.t.steps, 30u);
    activity_summary_t s;
    activity_day_init(&d, 0);
    activity_day_summary(&d, &p, 600, &s);
    EXPECT_EQ(s.kcal, 0u); // no BMR without a date
}

TEST(ActivityDay, RestoreKeepsTotals)
{
    const activity_profile_t p = profile();
    activity_day_t d;
    activity_day_init(&d, 20261005);
    activity_summary_t ended;
    activity_day_add(&d, &p, 20261005, 600, 1200, &ended);
    const activity_totals_t saved = d.t;
    activity_day_t r;
    activity_day_restore(&r, &saved);
    EXPECT_EQ(r.t.steps, 1200u);
    EXPECT_TRUE(r.t.step_goal_hit);
    EXPECT_EQ(activity_day_add(&r, &p, 20261005, 601, 10, &ended), 0u); // goal not reported twice
    EXPECT_EQ(r.t.steps, 1210u);
}
