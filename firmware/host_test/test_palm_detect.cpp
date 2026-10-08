// svc_input palm-cover detection (components/svc_input/palm_detect.c).
#include <gtest/gtest.h>

#include <cstdint>

#include "palm_detect.h"

namespace {

constexpr palm_cfg_t kCfg = {
    .width = 410, .height = 502, .cover_pct = 60, .area_min = 10, .max_points = 2, .hold_ms = 300};

constexpr palm_sample_t kFinger = {.points = 1, .area_max = 3, .x_min = 200, .y_min = 250, .x_max = 200, .y_max = 250};
constexpr palm_sample_t kBigArea = {.points = 1, .area_max = 12, .x_min = 200, .y_min = 250, .x_max = 200, .y_max = 250};
constexpr palm_sample_t kBlob = {.points = 15, .area_max = 0, .x_min = 0, .y_min = 0, .x_max = 0, .y_max = 0};
constexpr palm_sample_t kWideTwo = {.points = 2, .area_max = 2, .x_min = 10, .y_min = 10, .x_max = 400, .y_max = 490};
constexpr palm_sample_t kPinch = {.points = 2, .area_max = 2, .x_min = 150, .y_min = 200, .x_max = 260, .y_max = 300};
constexpr palm_sample_t kUp = {};

TEST(PalmCovers, Rules)
{
    EXPECT_FALSE(palm_sample_covers(&kCfg, &kUp));
    EXPECT_FALSE(palm_sample_covers(&kCfg, &kFinger));
    EXPECT_TRUE(palm_sample_covers(&kCfg, &kBigArea));
    EXPECT_TRUE(palm_sample_covers(&kCfg, &kBlob));
    EXPECT_TRUE(palm_sample_covers(&kCfg, &kWideTwo));
    EXPECT_FALSE(palm_sample_covers(&kCfg, &kPinch));
}

TEST(PalmCovers, BoxThresholdIsSixtyPercent)
{
    // 410 x 502 = 205820 px; 60 % = 123492. 300 x 412 = 123600 covers, 300 x 411 = 123300 not.
    palm_sample_t s = {.points = 2, .area_max = 0, .x_min = 0, .y_min = 0, .x_max = 299, .y_max = 411};
    EXPECT_TRUE(palm_sample_covers(&kCfg, &s));
    s.y_max = 410;
    EXPECT_FALSE(palm_sample_covers(&kCfg, &s));
}

TEST(PalmCovers, AreaRuleOff)
{
    palm_cfg_t cfg = kCfg;
    cfg.area_min = 0;
    EXPECT_FALSE(palm_sample_covers(&cfg, &kBigArea));
    EXPECT_TRUE(palm_sample_covers(&cfg, &kBlob));
}

class PalmDetect : public ::testing::Test {
protected:
    void SetUp() override { palm_detect_init(&p, &kCfg); }
    palm_detect_t p;
};

TEST_F(PalmDetect, FiresAfterHoldOncePerContact)
{
    EXPECT_FALSE(palm_detect_feed(&p, &kBlob, 0));
    EXPECT_FALSE(palm_detect_feed(&p, &kBlob, 299));
    EXPECT_TRUE(palm_detect_feed(&p, &kBlob, 300));
    EXPECT_FALSE(palm_detect_feed(&p, &kBlob, 310));
    EXPECT_FALSE(palm_detect_feed(&p, &kBlob, 2000));
    EXPECT_FALSE(palm_detect_feed(&p, &kUp, 2010));
    // A new contact can fire again.
    EXPECT_FALSE(palm_detect_feed(&p, &kBlob, 3000));
    EXPECT_TRUE(palm_detect_feed(&p, &kBlob, 3300));
}

TEST_F(PalmDetect, FingerNeverFires)
{
    for (uint32_t t = 0; t < 5000; t += 10) {
        EXPECT_FALSE(palm_detect_feed(&p, &kFinger, t));
    }
}

TEST_F(PalmDetect, BreakRestartsHold)
{
    EXPECT_FALSE(palm_detect_feed(&p, &kBlob, 0));
    EXPECT_FALSE(palm_detect_feed(&p, &kBlob, 200));
    EXPECT_FALSE(palm_detect_feed(&p, &kFinger, 210)); // still touching, not covering
    EXPECT_FALSE(palm_detect_feed(&p, &kBlob, 220));
    EXPECT_FALSE(palm_detect_feed(&p, &kBlob, 500));
    EXPECT_TRUE(palm_detect_feed(&p, &kBlob, 520));
}

TEST_F(PalmDetect, FingerThatSpreadsIntoPalm)
{
    EXPECT_FALSE(palm_detect_feed(&p, &kFinger, 0));
    EXPECT_FALSE(palm_detect_feed(&p, &kWideTwo, 100));
    EXPECT_TRUE(palm_detect_feed(&p, &kWideTwo, 400));
}

} // namespace
