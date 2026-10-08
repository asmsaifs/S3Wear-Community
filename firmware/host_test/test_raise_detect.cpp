// Raise-to-wake detector (svc_sensors/raise_detect.c).
#include <gtest/gtest.h>

#include <cmath>

extern "C" {
#include "raise_detect.h"
}

namespace {

constexpr uint32_t kStepMs = 20; // svc_sensors samples every 20 ms in a window
constexpr double kPi = 3.14159265358979;

struct Vec {
    int32_t x, y, z;
};

// Gravity at `deg` from screen-up, tilted toward 6 o'clock (-y): 0 = face up,
// 90 = screen facing sideways (arm hanging), 180 = face down.
Vec tilt(double deg, double g = 1000.0)
{
    const double r = deg * kPi / 180.0;
    return {0, static_cast<int32_t>(std::lround(-g * std::sin(r))), static_cast<int32_t>(std::lround(g * std::cos(r)))};
}

class RaiseTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        raise_init(&r, RAISE_CONE_MID_DEG);
    }

    raise_result_t feed(Vec v)
    {
        const raise_result_t res = raise_sample(&r, v.x, v.y, v.z, now);
        now += kStepMs;
        return res;
    }

    // Rotate from a to b degrees in `ms`, then hold b for `hold_ms`. Returns the
    // first non-CONTINUE result and the time it came at.
    raise_result_t sweep(double a, double b, uint32_t ms, uint32_t hold_ms, uint32_t *at = nullptr)
    {
        const uint32_t n = ms / kStepMs;
        for (uint32_t i = 0; n > 0 && i <= n; i++) {
            const uint32_t t = now;
            const raise_result_t res = feed(tilt(a + (b - a) * i / n));
            if (res != RAISE_CONTINUE) {
                if (at) {
                    *at = t;
                }
                return res;
            }
        }
        for (uint32_t i = 0; i < hold_ms / kStepMs; i++) {
            const uint32_t t = now;
            const raise_result_t res = feed(tilt(b));
            if (res != RAISE_CONTINUE) {
                if (at) {
                    *at = t;
                }
                return res;
            }
        }
        return RAISE_CONTINUE;
    }

    raise_detect_t r{};
    uint32_t now = 100000;
};

TEST(RaiseConfig, SensitivityToCone)
{
    EXPECT_EQ(raise_cone_deg(0), RAISE_CONE_LOW_DEG);
    EXPECT_EQ(raise_cone_deg(1), RAISE_CONE_MID_DEG);
    EXPECT_EQ(raise_cone_deg(2), RAISE_CONE_HIGH_DEG);
    EXPECT_EQ(raise_cone_deg(-1), RAISE_CONE_MID_DEG);
    EXPECT_EQ(raise_cone_deg(7), RAISE_CONE_MID_DEG);
}

TEST(RaiseConfig, Angle)
{
    EXPECT_EQ(raise_angle_deg(0, 0, 1000), 0);
    EXPECT_EQ(raise_angle_deg(0, 1000, 0), 90);
    EXPECT_EQ(raise_angle_deg(0, 0, -1000), 180);
    EXPECT_EQ(raise_angle_deg(0, 0, 0), 90);
    const Vec v = tilt(35);
    EXPECT_EQ(raise_angle_deg(v.x, v.y, v.z), 35);
}

TEST_F(RaiseTest, NoWindowNoWake)
{
    EXPECT_FALSE(raise_window_open(&r));
    EXPECT_EQ(feed(tilt(0)), RAISE_END);
}

TEST_F(RaiseTest, HangingToFaceUpWakes)
{
    raise_motion(&r, now);
    uint32_t at = 0;
    // 90 -> 10 degrees in 400 ms: the cone (35) is entered at ~68 % of the sweep.
    const uint32_t t0 = now;
    EXPECT_EQ(sweep(90, 10, 400, 500, &at), RAISE_WAKE);
    EXPECT_FALSE(raise_window_open(&r));
    // Entered the cone at ~t0+280 ms; wake RAISE_STABLE_MS later (one sample of slack).
    EXPECT_GE(at - t0, 280u + RAISE_STABLE_MS - kStepMs);
    EXPECT_LE(at - t0, 280u + RAISE_STABLE_MS + 2 * kStepMs);
}

TEST_F(RaiseTest, WakesWithinStableTimeAfterMotionEnds)
{
    raise_motion(&r, now);
    // Fast raise (150 ms), then hold: the wake comes RAISE_STABLE_MS after the hold starts.
    uint32_t at = 0;
    for (int i = 0; i < 8; i++) {
        feed(tilt(90 - i * 11, 1000 + (i % 2 ? 300 : -300))); // lifting: |a| far from 1 g
    }
    const uint32_t hold = now;
    EXPECT_EQ(sweep(10, 10, 0, 500, &at), RAISE_WAKE);
    EXPECT_LE(at - hold, RAISE_STABLE_MS + kStepMs);
}

TEST_F(RaiseTest, StartInConeNeverWakes)
{
    raise_motion(&r, now);
    // Typing: small tilts inside the cone and a few bumps.
    for (int i = 0; i < 40; i++) {
        const raise_result_t res = feed(tilt(5 + (i % 5) * 4, i % 7 == 0 ? 1300 : 1000));
        ASSERT_NE(res, RAISE_WAKE) << i;
        if (res == RAISE_END) {
            break;
        }
    }
}

TEST_F(RaiseTest, QuietWindowEnds)
{
    raise_motion(&r, now);
    const uint32_t t0 = now;
    uint32_t at = 0;
    EXPECT_EQ(sweep(20, 20, 0, 2000, &at), RAISE_END);
    EXPECT_GE(at - t0, RAISE_QUIET_MS - kStepMs);
    EXPECT_LE(at - t0, RAISE_QUIET_MS + kStepMs);
    EXPECT_FALSE(raise_window_open(&r));
}

TEST_F(RaiseTest, NotClearlyAwayDoesNotArm)
{
    raise_motion(&r, now);
    // 45 is outside the 35 cone but not by RAISE_ARM_MARGIN_DEG (needs > 50).
    EXPECT_EQ(sweep(45, 10, 300, 1500), RAISE_END);
}

TEST_F(RaiseTest, JustArmedWakes)
{
    raise_motion(&r, now);
    EXPECT_EQ(sweep(RAISE_CONE_MID_DEG + RAISE_ARM_MARGIN_DEG + 2, 10, 300, 500), RAISE_WAKE);
}

TEST_F(RaiseTest, ShakingThroughConeDoesNotWake)
{
    raise_motion(&r, now);
    // Arm swing while walking: big changes every sample, passes through the cone.
    for (int i = 0; i < 100; i++) {
        const double deg = (i % 4) * 40.0; // 0, 40, 80, 120, ...
        const raise_result_t res = feed(tilt(deg, i % 2 ? 1400 : 700));
        raise_motion(&r, now); // WoM keeps firing
        ASSERT_NE(res, RAISE_WAKE) << i;
        if (res == RAISE_END) {
            break;
        }
    }
}

TEST_F(RaiseTest, ContinuousMotionEndsAtMax)
{
    raise_motion(&r, now);
    const uint32_t t0 = now;
    raise_result_t res = RAISE_CONTINUE;
    uint32_t at = 0;
    for (int i = 0; i < 500 && res == RAISE_CONTINUE; i++) {
        at = now;
        res = feed(tilt(90 + (i % 2) * 30, 1000));
        raise_motion(&r, now);
    }
    EXPECT_EQ(res, RAISE_END);
    EXPECT_GE(at - t0, RAISE_MAX_MS - kStepMs);
    EXPECT_LE(at - t0, RAISE_MAX_MS + kStepMs);
}

TEST_F(RaiseTest, MotionExtendsWindow)
{
    raise_motion(&r, now);
    sweep(90, 90, 0, 800);
    raise_motion(&r, now); // more motion just before the quiet time runs out
    EXPECT_EQ(sweep(90, 90, 0, 800), RAISE_CONTINUE);
    EXPECT_TRUE(raise_window_open(&r));
}

TEST_F(RaiseTest, LowerThenRaiseAgainWakes)
{
    raise_motion(&r, now);
    // Looking, lowering the wrist away, raising it again in one window.
    EXPECT_EQ(sweep(10, 80, 300, 0), RAISE_CONTINUE);
    EXPECT_EQ(sweep(80, 10, 300, 500), RAISE_WAKE);
}

TEST_F(RaiseTest, FaceDownToUpWakes)
{
    raise_motion(&r, now);
    EXPECT_EQ(sweep(170, 15, 600, 500), RAISE_WAKE);
}

TEST_F(RaiseTest, NarrowConeRejectsWhatMidAccepts)
{
    raise_init(&r, RAISE_CONE_LOW_DEG);
    raise_motion(&r, now);
    EXPECT_EQ(sweep(90, 30, 300, 1500), RAISE_END); // 30 is outside 25
    raise_init(&r, RAISE_CONE_HIGH_DEG);
    raise_motion(&r, now);
    EXPECT_EQ(sweep(90, 40, 300, 500), RAISE_WAKE); // 40 is inside 45
}

TEST_F(RaiseTest, CancelClosesWindow)
{
    raise_motion(&r, now);
    raise_cancel(&r);
    EXPECT_FALSE(raise_window_open(&r));
    EXPECT_EQ(feed(tilt(90)), RAISE_END);
}

TEST_F(RaiseTest, WindowAfterAwayPoseIsArmed)
{
    raise_motion(&r, now);
    sweep(90, 90, 0, 2000); // ends quiet with the arm hanging
    ASSERT_FALSE(raise_window_open(&r));
    // A quick raise: the next window's first sample is already near the cone.
    raise_motion(&r, now);
    EXPECT_EQ(sweep(30, 10, 100, 500), RAISE_WAKE);
}

TEST_F(RaiseTest, WindowAfterConePoseIsNotArmed)
{
    raise_motion(&r, now);
    ASSERT_EQ(sweep(90, 15, 300, 500), RAISE_WAKE); // last pose in the cone
    raise_motion(&r, now);
    EXPECT_EQ(sweep(20, 10, 200, 1500), RAISE_END); // typing in the cone
}

TEST_F(RaiseTest, PoseArmsTheNextWindow)
{
    const Vec away = tilt(90);
    raise_pose(&r, away.x, away.y, away.z); // screen went off with the arm down
    raise_motion(&r, now);
    EXPECT_EQ(sweep(25, 10, 100, 500), RAISE_WAKE);
}

TEST_F(RaiseTest, UnknownOrConePoseDoesNotArm)
{
    raise_motion(&r, now); // no pose yet: counted as screen up
    EXPECT_EQ(sweep(20, 10, 200, 1500), RAISE_END);
    const Vec up = tilt(10);
    raise_pose(&r, up.x, up.y, up.z);
    raise_motion(&r, now);
    EXPECT_EQ(sweep(20, 10, 200, 1500), RAISE_END);
}

} // namespace
