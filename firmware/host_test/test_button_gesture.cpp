// svc_input button press recognizer (components/svc_input/button_gesture.c).
#include <gtest/gtest.h>

#include <cstdint>

#include "button_gesture.h"

namespace {

constexpr btn_gesture_cfg_t kPower = {.long_ms = 2000, .gap_ms = 300, .max_clicks = 3};
constexpr btn_gesture_cfg_t kBack = {.long_ms = 1000, .gap_ms = 300, .max_clicks = 1};

class ButtonGesture : public ::testing::Test {
protected:
    void Use(const btn_gesture_cfg_t &cfg) { btn_gesture_init(&f, &cfg); }
    // Press at t, release at t + held; returns what the release reported.
    btn_gesture_t Click(uint32_t t, uint32_t held = 80)
    {
        EXPECT_EQ(btn_gesture_press(&f, t), BTN_GESTURE_NONE);
        return btn_gesture_release(&f, t + held);
    }
    btn_gesture_fsm_t f;
};

TEST_F(ButtonGesture, SingleClickWithoutMultiIsInstant)
{
    Use(kBack);
    EXPECT_EQ(Click(100), BTN_GESTURE_SHORT);
    EXPECT_EQ(btn_gesture_ms_to_next(&f, 180), BTN_GESTURE_NO_DEADLINE);
}

TEST_F(ButtonGesture, ShortWaitsForGap)
{
    Use(kPower);
    EXPECT_EQ(Click(100), BTN_GESTURE_NONE);
    EXPECT_EQ(btn_gesture_ms_to_next(&f, 180), 300u);
    EXPECT_EQ(btn_gesture_tick(&f, 479), BTN_GESTURE_NONE);
    EXPECT_EQ(btn_gesture_tick(&f, 480), BTN_GESTURE_SHORT);
    EXPECT_EQ(btn_gesture_ms_to_next(&f, 480), BTN_GESTURE_NO_DEADLINE);
}

TEST_F(ButtonGesture, DoubleAfterGap)
{
    Use(kPower);
    EXPECT_EQ(Click(0), BTN_GESTURE_NONE);
    EXPECT_EQ(Click(200), BTN_GESTURE_NONE);
    EXPECT_EQ(btn_gesture_tick(&f, 579), BTN_GESTURE_NONE);
    EXPECT_EQ(btn_gesture_tick(&f, 580), BTN_GESTURE_DOUBLE);
}

TEST_F(ButtonGesture, TripleIsReportedOnThirdRelease)
{
    Use(kPower);
    EXPECT_EQ(Click(0), BTN_GESTURE_NONE);
    EXPECT_EQ(Click(250), BTN_GESTURE_NONE);
    EXPECT_EQ(Click(500), BTN_GESTURE_TRIPLE);
    EXPECT_EQ(btn_gesture_ms_to_next(&f, 600), BTN_GESTURE_NO_DEADLINE);
    // The next press starts a new gesture.
    EXPECT_EQ(Click(700), BTN_GESTURE_NONE);
    EXPECT_EQ(btn_gesture_tick(&f, 1080), BTN_GESTURE_SHORT);
}

TEST_F(ButtonGesture, DoubleMaxReportsAtOnce)
{
    Use({.long_ms = 0, .gap_ms = 300, .max_clicks = 2});
    EXPECT_EQ(Click(0), BTN_GESTURE_NONE);
    EXPECT_EQ(Click(200), BTN_GESTURE_DOUBLE);
}

TEST_F(ButtonGesture, PressAfterGapWithoutTickEndsPreviousGesture)
{
    Use(kPower);
    EXPECT_EQ(Click(0), BTN_GESTURE_NONE);
    EXPECT_EQ(btn_gesture_press(&f, 1000), BTN_GESTURE_SHORT); // tick missed: reported by the press
    EXPECT_EQ(btn_gesture_release(&f, 1080), BTN_GESTURE_NONE);
    EXPECT_EQ(btn_gesture_tick(&f, 1380), BTN_GESTURE_SHORT);
}

TEST_F(ButtonGesture, LongWhileHeldThenReleaseIsSilent)
{
    Use(kPower);
    EXPECT_EQ(btn_gesture_press(&f, 0), BTN_GESTURE_NONE);
    EXPECT_EQ(btn_gesture_ms_to_next(&f, 500), 1500u);
    EXPECT_EQ(btn_gesture_tick(&f, 1999), BTN_GESTURE_NONE);
    EXPECT_EQ(btn_gesture_tick(&f, 2000), BTN_GESTURE_LONG);
    EXPECT_EQ(btn_gesture_ms_to_next(&f, 2000), BTN_GESTURE_NO_DEADLINE);
    EXPECT_EQ(btn_gesture_tick(&f, 5000), BTN_GESTURE_NONE);
    EXPECT_EQ(btn_gesture_release(&f, 5000), BTN_GESTURE_NONE);
    EXPECT_EQ(btn_gesture_ms_to_next(&f, 5000), BTN_GESTURE_NO_DEADLINE);
}

TEST_F(ButtonGesture, LongSeenOnlyAtReleaseIsStillLong)
{
    Use(kBack);
    EXPECT_EQ(btn_gesture_press(&f, 0), BTN_GESTURE_NONE);
    EXPECT_EQ(btn_gesture_release(&f, 1500), BTN_GESTURE_LONG);
}

TEST_F(ButtonGesture, NoLongWhenDisabled)
{
    Use({.long_ms = 0, .gap_ms = 300, .max_clicks = 1});
    EXPECT_EQ(btn_gesture_press(&f, 0), BTN_GESTURE_NONE);
    EXPECT_EQ(btn_gesture_ms_to_next(&f, 0), BTN_GESTURE_NO_DEADLINE);
    EXPECT_EQ(btn_gesture_release(&f, 5000), BTN_GESTURE_SHORT);
}

TEST_F(ButtonGesture, HoldAfterFirstClickIsNotLong)
{
    Use(kPower);
    EXPECT_EQ(Click(0), BTN_GESTURE_NONE);
    EXPECT_EQ(btn_gesture_press(&f, 200), BTN_GESTURE_NONE);
    EXPECT_EQ(btn_gesture_tick(&f, 3000), BTN_GESTURE_NONE);
    EXPECT_EQ(btn_gesture_release(&f, 3000), BTN_GESTURE_NONE);
    EXPECT_EQ(btn_gesture_tick(&f, 3300), BTN_GESTURE_DOUBLE);
}

TEST_F(ButtonGesture, CancelDropsPendingGesture)
{
    Use(kPower);
    EXPECT_EQ(Click(0), BTN_GESTURE_NONE);
    btn_gesture_cancel(&f);
    EXPECT_EQ(btn_gesture_tick(&f, 1000), BTN_GESTURE_NONE);
    // Cancel while held: the release reports nothing either.
    EXPECT_EQ(btn_gesture_press(&f, 2000), BTN_GESTURE_NONE);
    btn_gesture_cancel(&f);
    EXPECT_EQ(btn_gesture_release(&f, 2050), BTN_GESTURE_NONE);
    EXPECT_EQ(btn_gesture_tick(&f, 9000), BTN_GESTURE_NONE);
}

TEST_F(ButtonGesture, ReleaseWithoutPressIsIgnored)
{
    Use(kBack);
    EXPECT_EQ(btn_gesture_release(&f, 10), BTN_GESTURE_NONE);
}

TEST_F(ButtonGesture, MaxClicksIsClamped)
{
    Use({.long_ms = 0, .gap_ms = 300, .max_clicks = 0});
    EXPECT_EQ(f.cfg.max_clicks, 1);
    Use({.long_ms = 0, .gap_ms = 300, .max_clicks = 9});
    EXPECT_EQ(f.cfg.max_clicks, BTN_GESTURE_MAX_CLICKS);
}

TEST_F(ButtonGesture, TimeWrapAround)
{
    Use(kPower);
    const uint32_t t0 = UINT32_MAX - 100;
    EXPECT_EQ(Click(t0), BTN_GESTURE_NONE); // released at t0 + 80
    EXPECT_EQ(btn_gesture_tick(&f, t0 + 379), BTN_GESTURE_NONE);
    EXPECT_EQ(btn_gesture_tick(&f, t0 + 380), BTN_GESTURE_SHORT);
}

TEST(ButtonGestureName, Names)
{
    EXPECT_STREQ(btn_gesture_name(BTN_GESTURE_TRIPLE), "triple");
    EXPECT_STREQ(btn_gesture_name(BTN_GESTURE_COUNT), "?");
}

} // namespace
