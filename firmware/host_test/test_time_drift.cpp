// time_drift: RTC drift calibration windows and system-clock discipline.
#include <gtest/gtest.h>

#include <cmath>

#include "time_drift.h"

namespace {

constexpr int64_t kT0 = 1791022140000LL; // 2026-10-03 10:09:00 UTC, ms
constexpr int64_t kHourMs = 3600LL * 1000;

// A PCF85063 running `ppm` fast (with the current steps already in that number). It is
// rewritten exactly on the next whole second when asked, and read just before each sync.
struct FakeRtc {
    double ppm = 0;
    int64_t set_at_ms = kT0; // true time of the last rewrite (a whole second)
    int64_t read(int64_t ref_ms) const
    {
        const double elapsed = (double)(ref_ms - set_at_ms);
        return (int64_t)std::floor(((double)set_at_ms + elapsed * (1.0 + ppm * 1e-6)) / 1000.0);
    }
    void rewrite(int64_t ref_ms) { set_at_ms = (ref_ms + 999) / 1000 * 1000; }
};

struct Rig {
    time_drift_t d;
    FakeRtc rtc;
    int64_t now = kT0;
    uint32_t seed = 12345;
    int rewrites = 0;

    Rig(int8_t steps, double ppm)
    {
        time_drift_init(&d, steps);
        rtc.ppm = ppm;
        sync(0);
    }
    // One phone sync after `gap_ms`, at a random sub-second phase (deterministic sequence).
    time_drift_result_t sync(int64_t gap_ms)
    {
        seed = seed * 1103515245u + 12345u;
        now += gap_ms + (gap_ms ? (seed >> 8) % 1000 : 0);
        bool rewrite = false;
        const time_drift_result_t r = time_drift_on_sync(&d, now, rtc.read(now), true, &rewrite);
        if (rewrite) {
            rtc.rewrite(now);
            rewrites++;
        }
        return r;
    }
    // Syncs every ~6 h until a window completes.
    void window()
    {
        while (sync(6 * kHourMs) != TIME_DRIFT_ADJUSTED) {
        }
    }
};

} // namespace

TEST(TimeDrift, FirstSyncAnchorsAndRewrites)
{
    time_drift_t d;
    time_drift_init(&d, 0);
    bool rewrite = false;
    EXPECT_EQ(time_drift_on_sync(&d, kT0, kT0 / 1000, true, &rewrite), TIME_DRIFT_ANCHORED);
    EXPECT_TRUE(rewrite);
    EXPECT_EQ(d.anchor_ms, kT0);
    EXPECT_EQ(d.err_ms, 0);
}

TEST(TimeDrift, InvalidRtcRestartsWindow)
{
    time_drift_t d;
    time_drift_init(&d, 3);
    bool rewrite = false;
    time_drift_on_sync(&d, kT0, kT0 / 1000, true, &rewrite);
    rewrite = false;
    EXPECT_EQ(time_drift_on_sync(&d, kT0 + 6 * kHourMs, 0, false, &rewrite), TIME_DRIFT_ANCHORED);
    EXPECT_TRUE(rewrite);
    EXPECT_EQ(d.anchor_ms, kT0 + 6 * kHourMs);
    EXPECT_EQ(d.steps, 3);
}

TEST(TimeDrift, MeasuresUntilMinimumSpan)
{
    Rig r(0, 20);
    for (int i = 0; i < 7; i++) {
        EXPECT_EQ(r.sync(6 * kHourMs), TIME_DRIFT_MEASURING) << i; // up to 42 h
    }
    EXPECT_EQ(r.sync(6 * kHourMs), TIME_DRIFT_ADJUSTED); // 48 h
}

// Mid-window the RTC is left alone while it is within 1 s, so readings do not add noise;
// the system clock is told how far off it is instead.
TEST(TimeDrift, RewritesOnlyWhenOffBySecond)
{
    Rig r(0, 20); // 1.7 s/day
    const int before = r.rewrites;
    r.sync(6 * kHourMs); // ~0.43 s off
    EXPECT_EQ(r.rewrites, before);
    for (int i = 0; i < 4 && r.rewrites == before; i++) {
        r.sync(6 * kHourMs);
    }
    EXPECT_EQ(r.rewrites, before + 1);
    EXPECT_EQ(r.d.rtc_bias_ms, 0);
}

// A crystal 20 ppm fast (1.7 s/day) gets ~+5 steps (4.34 ppm each) and stays there.
TEST(TimeDrift, FastRtcConverges)
{
    const double crystal_ppm = 20;
    Rig r(0, crystal_ppm);
    for (int w = 0; w < 8; w++) {
        r.window();
        r.rtc.ppm = crystal_ppm - r.d.steps * 4.34; // the offset register slows it
        EXPECT_GE(r.d.steps, 4) << "window " << w;
        EXPECT_LE(r.d.steps, 6) << "window " << w;
    }
    EXPECT_LT(std::fabs(r.rtc.ppm), 23.0 / 2); // spec: +/-2 s/day = 23 ppm
}

// -50 ppm (4.3 s/day): the RTC is rewritten at every sync in the first window, so that
// estimate is coarse (+/-2 steps); the next window, with the small residual, refines it.
TEST(TimeDrift, SlowRtcGetsNegativeSteps)
{
    const double crystal_ppm = -50;
    Rig r(0, crystal_ppm);
    r.window();
    EXPECT_NEAR(r.d.steps, -12, 2); // -50 / 4.34 = -11.5
    EXPECT_NEAR(r.d.last_ppb, -50000, 10000);
    for (int w = 0; w < 4; w++) {
        r.rtc.ppm = crystal_ppm - r.d.steps * 4.34;
        r.window();
    }
    r.rtc.ppm = crystal_ppm - r.d.steps * 4.34;
    EXPECT_LT(std::fabs(r.rtc.ppm), 23.0 / 2);
}

TEST(TimeDrift, ExactRtcKeepsSteps)
{
    Rig r(7, 0);
    for (int w = 0; w < 20; w++) {
        r.window();
        EXPECT_EQ(r.d.steps, 7) << "window " << w;
    }
}

// 3 ppm (0.26 s/day) is inside the deadband: not worth chasing through 1 s readings.
TEST(TimeDrift, SmallDriftInsideDeadband)
{
    Rig r(-3, 3);
    for (int w = 0; w < 20; w++) {
        r.window();
        EXPECT_EQ(r.d.steps, -3) << "window " << w;
    }
}

TEST(TimeDrift, StepsClampToRegisterRange)
{
    Rig r(60, 150);
    r.window();
    EXPECT_EQ(r.d.steps, TIME_DRIFT_STEPS_MAX);
    time_drift_t d;
    time_drift_init(&d, -100);
    EXPECT_EQ(d.steps, TIME_DRIFT_STEPS_MIN);
}

// Someone set the RTC by hand (or the phone's clock jumped): minutes of error in 6 h
// is not drift, so the window restarts instead of programming nonsense.
TEST(TimeDrift, RejectsImplausibleError)
{
    time_drift_t d;
    time_drift_init(&d, 2);
    bool rewrite = false;
    time_drift_on_sync(&d, kT0, kT0 / 1000, true, &rewrite);
    const int64_t later = kT0 + 6 * kHourMs;
    rewrite = false;
    EXPECT_EQ(time_drift_on_sync(&d, later, later / 1000 + 120, true, &rewrite), TIME_DRIFT_REJECTED);
    EXPECT_TRUE(rewrite);
    EXPECT_EQ(d.anchor_ms, later);
    EXPECT_EQ(d.err_ms, 0);
    EXPECT_EQ(d.steps, 2);
    // Reference going backwards is rejected too.
    EXPECT_EQ(time_drift_on_sync(&d, later - 1000, later / 1000, true, &rewrite), TIME_DRIFT_REJECTED);
}

TEST(TimeDrift, ResetWindowDropsMeasurement)
{
    time_drift_t d;
    time_drift_init(&d, 0);
    bool rewrite = false;
    time_drift_on_sync(&d, kT0, kT0 / 1000, true, &rewrite);
    time_drift_reset_window(&d);
    EXPECT_EQ(time_drift_on_sync(&d, kT0 + kHourMs, (kT0 + kHourMs) / 1000, true, &rewrite), TIME_DRIFT_ANCHORED);
}

TEST(TimeDiscipline, WithinToleranceNoCorrection)
{
    time_drift_t d;
    time_drift_init(&d, 0);
    // RTC reads 100 s: true time is 100.0..100.999 s, best estimate 100.5 s.
    EXPECT_EQ(time_discipline_correction(&d, 100500, 100), 0);
    EXPECT_EQ(time_discipline_correction(&d, 100000, 100), 0);
    EXPECT_EQ(time_discipline_correction(&d, 101400, 100), 0);
    EXPECT_EQ(time_discipline_correction(&d, 99600, 100), 0);
}

TEST(TimeDiscipline, StepsBackToRtc)
{
    time_drift_t d;
    time_drift_init(&d, 0);
    // System clock 3 s fast after a long light sleep on the RC oscillator.
    EXPECT_EQ(time_discipline_correction(&d, 103500, 100), -3000);
    // 2 s slow.
    EXPECT_EQ(time_discipline_correction(&d, 98500, 100), 2000);
}

TEST(TimeDiscipline, UsesKnownRtcBias)
{
    time_drift_t d;
    time_drift_init(&d, 0);
    d.rtc_bias_ms = 900; // RTC known to be 0.9 s fast since the last sync
    EXPECT_EQ(time_discipline_correction(&d, 100000, 101), 0);
    EXPECT_EQ(time_discipline_correction(&d, 102000, 101), -1400);
}
