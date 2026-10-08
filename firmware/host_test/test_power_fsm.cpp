// svc_power state machine (components/svc_power/power_fsm.c).
#include <gtest/gtest.h>

#include <cstdint>

#include "power_fsm.h"

namespace {

constexpr power_policy_t kDefault = {.timeout_ms = 10000, .aod = false, .saver = false};

class PowerFsm : public ::testing::Test {
protected:
    void SetUp() override { power_fsm_init(&f, &kDefault, 0); }
    power_fsm_t f;
};

TEST_F(PowerFsm, StartsActive)
{
    EXPECT_EQ(f.state, POWER_STATE_ACTIVE);
    EXPECT_EQ(power_fsm_ms_to_next(&f, 0), 7000u); // DIM 3 s before the 10 s timeout
}

TEST_F(PowerFsm, DimsThenSleepsOnTimeout)
{
    EXPECT_FALSE(power_fsm_tick(&f, 6999));
    EXPECT_TRUE(power_fsm_tick(&f, 7000));
    EXPECT_EQ(f.state, POWER_STATE_DIM);
    EXPECT_EQ(power_fsm_ms_to_next(&f, 7000), 3000u);
    EXPECT_FALSE(power_fsm_tick(&f, 9999));
    EXPECT_TRUE(power_fsm_tick(&f, 10000));
    EXPECT_EQ(f.state, POWER_STATE_SLEEP);
    EXPECT_EQ(power_fsm_ms_to_next(&f, 10000), POWER_FSM_NO_DEADLINE);
}

TEST_F(PowerFsm, LateTickGoesStraightToSleep)
{
    EXPECT_TRUE(power_fsm_tick(&f, 60000));
    EXPECT_EQ(f.state, POWER_STATE_SLEEP);
}

TEST_F(PowerFsm, ActivityRestartsTimerAndUndims)
{
    power_fsm_tick(&f, 7500);
    ASSERT_EQ(f.state, POWER_STATE_DIM);
    EXPECT_TRUE(power_fsm_activity(&f, 8000));
    EXPECT_EQ(f.state, POWER_STATE_ACTIVE);
    EXPECT_EQ(power_fsm_ms_to_next(&f, 8000), 7000u);
    EXPECT_FALSE(power_fsm_tick(&f, 14999));
    EXPECT_TRUE(power_fsm_tick(&f, 15000));
    EXPECT_EQ(f.state, POWER_STATE_DIM);
}

TEST_F(PowerFsm, ActivityWakesFromEveryScreenOffState)
{
    for (const bool aod : {false, true}) {
        for (const bool saver : {false, true}) {
            const power_policy_t p = {.timeout_ms = 10000, .aod = aod, .saver = saver};
            power_fsm_init(&f, &p, 0);
            ASSERT_TRUE(power_fsm_tick(&f, 10000));
            const power_state_t off = saver ? POWER_STATE_SAVER : aod ? POWER_STATE_AOD : POWER_STATE_SLEEP;
            EXPECT_EQ(f.state, off);
            EXPECT_TRUE(power_fsm_activity(&f, 20000));
            EXPECT_EQ(f.state, POWER_STATE_ACTIVE);
        }
    }
}

TEST_F(PowerFsm, ScreenOffIgnoresHoldsAndTimer)
{
    power_fsm_hold(&f, true, 0);
    EXPECT_TRUE(power_fsm_screen_off(&f, 100));
    EXPECT_EQ(f.state, POWER_STATE_SLEEP);
    EXPECT_FALSE(power_fsm_screen_off(&f, 200)); // already off
}

TEST_F(PowerFsm, HoldBlocksTimeoutAndReleaseRestartsIt)
{
    power_fsm_hold(&f, true, 1000);
    power_fsm_hold(&f, true, 1000);
    EXPECT_EQ(power_fsm_ms_to_next(&f, 1000), POWER_FSM_NO_DEADLINE);
    EXPECT_FALSE(power_fsm_tick(&f, 100000));
    EXPECT_EQ(f.state, POWER_STATE_ACTIVE);
    power_fsm_hold(&f, false, 100000);
    EXPECT_FALSE(power_fsm_tick(&f, 200000)); // one holder left
    power_fsm_hold(&f, false, 200000);
    EXPECT_EQ(power_fsm_ms_to_next(&f, 200000), 7000u);
    EXPECT_TRUE(power_fsm_tick(&f, 210000));
    EXPECT_EQ(f.state, POWER_STATE_SLEEP);
    power_fsm_hold(&f, false, 0); // unbalanced release is ignored
    EXPECT_EQ(f.holds, 0);
}

TEST_F(PowerFsm, ShortTimeoutStillDims)
{
    const power_policy_t p = {.timeout_ms = 5000, .aod = false, .saver = false};
    power_fsm_init(&f, &p, 0);
    EXPECT_EQ(power_fsm_ms_to_next(&f, 0), 2000u);
    const power_policy_t tiny = {.timeout_ms = 2000, .aod = false, .saver = false};
    power_fsm_init(&f, &tiny, 0);
    EXPECT_EQ(power_fsm_ms_to_next(&f, 0), 2000u); // no DIM: straight to SLEEP
    EXPECT_TRUE(power_fsm_tick(&f, 2000));
    EXPECT_EQ(f.state, POWER_STATE_SLEEP);
}

TEST_F(PowerFsm, PolicyChangeWhileOffFollows)
{
    power_fsm_screen_off(&f, 0);
    ASSERT_EQ(f.state, POWER_STATE_SLEEP);
    power_policy_t p = kDefault;
    p.aod = true;
    EXPECT_TRUE(power_fsm_set_policy(&f, &p));
    EXPECT_EQ(f.state, POWER_STATE_AOD);
    p.saver = true; // saver wins over AOD
    EXPECT_TRUE(power_fsm_set_policy(&f, &p));
    EXPECT_EQ(f.state, POWER_STATE_SAVER);
    p.saver = false;
    p.aod = false;
    EXPECT_TRUE(power_fsm_set_policy(&f, &p));
    EXPECT_EQ(f.state, POWER_STATE_SLEEP);
}

TEST_F(PowerFsm, PolicyChangeWhileOnKeepsState)
{
    power_policy_t p = kDefault;
    p.timeout_ms = 30000;
    EXPECT_FALSE(power_fsm_set_policy(&f, &p));
    EXPECT_EQ(f.state, POWER_STATE_ACTIVE);
    EXPECT_EQ(power_fsm_ms_to_next(&f, 0), 27000u);
}

TEST_F(PowerFsm, CriticalBatteryNeedsTwoReadsWithoutUsb)
{
    EXPECT_FALSE(power_fsm_battery(&f, 3, false));
    EXPECT_FALSE(power_fsm_battery(&f, 50, false)); // one good read resets the count
    EXPECT_FALSE(power_fsm_battery(&f, 2, false));
    EXPECT_FALSE(power_fsm_battery(&f, 2, true)); // USB power: never
    EXPECT_FALSE(power_fsm_battery(&f, -1, false)); // no battery
    EXPECT_FALSE(power_fsm_battery(&f, 2, false));
    EXPECT_TRUE(power_fsm_battery(&f, 1, false));
    EXPECT_EQ(f.state, POWER_STATE_WATCH_ONLY);
    EXPECT_FALSE(power_fsm_activity(&f, 0)); // only a reboot leaves WATCH-ONLY
    EXPECT_EQ(f.state, POWER_STATE_WATCH_ONLY);
}

TEST_F(PowerFsm, EnterOnlyWatchOnlyOrOff)
{
    EXPECT_FALSE(power_fsm_enter(&f, POWER_STATE_SLEEP));
    EXPECT_FALSE(power_fsm_enter(&f, POWER_STATE_ACTIVE));
    EXPECT_TRUE(power_fsm_enter(&f, POWER_STATE_OFF));
    EXPECT_EQ(f.state, POWER_STATE_OFF);
    EXPECT_FALSE(power_fsm_battery(&f, 0, false));
    EXPECT_FALSE(power_fsm_battery(&f, 0, false));
    EXPECT_EQ(f.state, POWER_STATE_OFF);
}

TEST_F(PowerFsm, TimerSurvivesWrapAround)
{
    const uint32_t start = UINT32_MAX - 1000;
    power_fsm_init(&f, &kDefault, start);
    EXPECT_EQ(power_fsm_ms_to_next(&f, start + 2000), 5000u);
    EXPECT_FALSE(power_fsm_tick(&f, start + 6999));
    EXPECT_TRUE(power_fsm_tick(&f, start + 10000));
    EXPECT_EQ(f.state, POWER_STATE_SLEEP);
}

TEST(PowerState, Predicates)
{
    EXPECT_TRUE(power_state_screen_on(POWER_STATE_DIM));
    EXPECT_FALSE(power_state_screen_on(POWER_STATE_AOD));
    EXPECT_TRUE(power_state_panel_on(POWER_STATE_AOD));
    EXPECT_FALSE(power_state_panel_on(POWER_STATE_SAVER));
    EXPECT_STREQ(power_state_name(POWER_STATE_WATCH_ONLY), "WATCH-ONLY");
    EXPECT_STREQ(power_state_name(POWER_STATE_COUNT), "?");
}

} // namespace
