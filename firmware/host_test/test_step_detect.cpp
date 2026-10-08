// step_detect: every accelerometer log in firmware/test/activity/*.csv (the `imu log`
// format, README.md there) must give its counted steps within the tolerance; plus unit
// cases for the run logic.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "step_detect.h"

namespace {

struct Log {
    std::string name;
    long steps = -1; // "# steps=N": the true count
    long tol = -1;   // "# tol=N": allowed error in steps; default 7 % of steps
    std::vector<uint32_t> t;
    std::vector<int32_t> x, y, z;
};

bool load(const std::filesystem::path &p, Log *log)
{
    std::ifstream in(p);
    if (!in) {
        return false;
    }
    log->name = p.filename().string();
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        if (line[0] == '#') {
            long v = 0;
            if (std::sscanf(line.c_str(), "# steps=%ld", &v) == 1) {
                log->steps = v;
            } else if (std::sscanf(line.c_str(), "# tol=%ld", &v) == 1) {
                log->tol = v;
            }
            continue;
        }
        unsigned long t = 0;
        long x = 0, y = 0, z = 0;
        if (std::sscanf(line.c_str(), "%lu,%ld,%ld,%ld", &t, &x, &y, &z) == 4) {
            log->t.push_back((uint32_t)t);
            log->x.push_back((int32_t)x);
            log->y.push_back((int32_t)y);
            log->z.push_back((int32_t)z);
        }
    }
    return true;
}

uint32_t run(const Log &log)
{
    step_detect_t sd;
    step_detect_init(&sd);
    uint32_t steps = 0;
    for (size_t i = 0; i < log.t.size(); i++) {
        steps += step_detect_sample(&sd, log.t[i], log.x[i], log.y[i], log.z[i]);
    }
    EXPECT_EQ(steps, sd.total);
    return steps;
}

// Steps at a fixed interval: a clean sine bump per step on top of 1 g.
uint32_t feed_steps(step_detect_t *sd, uint32_t *t, int n, uint32_t interval_ms, int amp_mg, uint32_t rate_ms = 20)
{
    uint32_t steps = 0;
    const uint32_t end = *t + (uint32_t)n * interval_ms;
    for (; *t < end; *t += rate_ms) {
        const double ph = 2 * M_PI * (double)(*t % interval_ms) / interval_ms;
        steps += step_detect_sample(sd, *t, 0, 0, 1000 + (int32_t)(amp_mg * std::sin(ph)));
    }
    return steps;
}

uint32_t feed_still(step_detect_t *sd, uint32_t *t, uint32_t ms, uint32_t rate_ms = 20)
{
    uint32_t steps = 0;
    const uint32_t end = *t + ms;
    for (; *t < end; *t += rate_ms) {
        steps += step_detect_sample(sd, *t, 0, 0, 1000);
    }
    return steps;
}

} // namespace

TEST(StepDetect, RecordedLogs)
{
    const std::filesystem::path dir = S3W_ACTIVITY_LOGS_DIR;
    int files = 0;
    for (const auto &e : std::filesystem::directory_iterator(dir)) {
        if (e.path().extension() != ".csv") {
            continue;
        }
        Log log;
        ASSERT_TRUE(load(e.path(), &log)) << e.path();
        ASSERT_GE(log.steps, 0) << log.name << ": no '# steps=N' line";
        ASSERT_FALSE(log.t.empty()) << log.name;
        const long tol = log.tol >= 0 ? log.tol : (long)std::ceil(log.steps * 0.07);
        const long got = (long)run(log);
        EXPECT_LE(std::labs(got - log.steps), tol)
            << log.name << ": counted " << got << ", true " << log.steps << " (tolerance " << tol << ")";
        std::printf("  %-34s true %5ld  counted %5ld  (%+.1f %%)\n", log.name.c_str(), log.steps, got,
                    log.steps ? 100.0 * (got - log.steps) / log.steps : 0.0);
        files++;
    }
    EXPECT_GT(files, 0) << "no logs in " << dir;
}

TEST(StepDetect, RunCountsOnlyAfterEightSteps)
{
    step_detect_t sd;
    step_detect_init(&sd);
    uint32_t t = 0;
    EXPECT_EQ(feed_still(&sd, &t, 2000), 0u);
    // 6 rhythmic steps then a stop: never a run, nothing counted.
    EXPECT_EQ(feed_steps(&sd, &t, 6, 550, 250), 0u);
    EXPECT_EQ(feed_still(&sd, &t, 3000), 0u);
    // 20 steps: all of them count (the first run's worth at once).
    const uint32_t n = feed_steps(&sd, &t, 20, 550, 250);
    EXPECT_GE(n, 19u);
    EXPECT_LE(n, 20u);
    EXPECT_NEAR(step_detect_cadence(&sd), 109, 3);
    EXPECT_EQ(feed_still(&sd, &t, 3000), 0u);
    EXPECT_EQ(step_detect_cadence(&sd), 0); // run over
}

TEST(StepDetect, IrregularPeaksNeverCount)
{
    step_detect_t sd;
    step_detect_init(&sd);
    uint32_t t = 0;
    feed_still(&sd, &t, 2000);
    // Big single movements at changing intervals (0.3, 1.1, 0.4, 1.5 s, ...).
    const uint32_t gaps[] = {300, 1100, 400, 1500, 350, 1200, 450, 1700, 300, 1000, 380, 1400};
    uint32_t steps = 0;
    for (uint32_t g : gaps) {
        steps += feed_steps(&sd, &t, 1, g, 400);
    }
    EXPECT_EQ(steps, 0u);
}

TEST(StepDetect, SmallMotionIsNotAStep)
{
    step_detect_t sd;
    step_detect_init(&sd);
    uint32_t t = 0;
    feed_still(&sd, &t, 2000);
    EXPECT_EQ(feed_steps(&sd, &t, 60, 500, 30), 0u); // 30 mg: below STEP_TH_MIN_MG
}

TEST(StepDetect, RateAndGap)
{
    step_detect_t sd;
    step_detect_init(&sd);
    uint32_t t = 0;
    feed_still(&sd, &t, 1000, 48);
    uint32_t n = feed_steps(&sd, &t, 30, 520, 250, 48); // 21 Hz
    n += feed_steps(&sd, &t, 30, 520, 250, 16);         // 62.5 Hz: the run goes on
    EXPECT_NEAR((double)n, 60.0, 2.0);
    // A 1.5 s gap in the samples restarts the filters and the run.
    t += 1500;
    EXPECT_EQ(feed_steps(&sd, &t, 5, 520, 250, 48), 0u);
}
