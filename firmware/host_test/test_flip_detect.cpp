// flip_detect: face up -> face down and still -> one flip; noise and a watch that
// starts face down do not count.
#include <gtest/gtest.h>

#include "flip_detect.h"

namespace {

// Feed one orientation every 100 ms for ms; returns how many flips were reported.
int feed(flip_detect_t *f, int32_t x, int32_t y, int32_t z, uint32_t *now, uint32_t ms)
{
    int flips = 0;
    for (uint32_t t = 0; t < ms; t += 100) {
        flips += flip_sample(f, x, y, z, *now) ? 1 : 0;
        *now += 100;
    }
    return flips;
}

} // namespace

TEST(FlipDetect, FaceUpThenDown)
{
    flip_detect_t f;
    flip_init(&f);
    uint32_t now = 0;
    EXPECT_EQ(feed(&f, 0, 0, 1000, &now, 500), 0);   // face up
    EXPECT_EQ(feed(&f, 0, 700, -700, &now, 300), 0);  // turning (z not low enough)
    EXPECT_EQ(feed(&f, 0, 0, -990, &now, 300), 0);    // down, not long enough yet
    EXPECT_EQ(feed(&f, 0, 0, -990, &now, 1000), 1);   // held: exactly one flip
    EXPECT_EQ(feed(&f, 0, 0, -990, &now, 2000), 0);   // stays down: no repeat
    // Up and down again: a second flip.
    EXPECT_EQ(feed(&f, 0, 0, 1000, &now, 200), 0);
    EXPECT_EQ(feed(&f, 100, 0, -980, &now, 1000), 1);
}

TEST(FlipDetect, StartsFaceDown)
{
    flip_detect_t f;
    flip_init(&f);
    uint32_t now = 0;
    EXPECT_EQ(feed(&f, 0, 0, -1000, &now, 3000), 0); // never armed
    EXPECT_EQ(feed(&f, 0, 1000, 0, &now, 200), 0);   // on its side: arms
    EXPECT_EQ(feed(&f, 0, 0, -1000, &now, 1000), 1);
}

TEST(FlipDetect, ShakingIsNotStill)
{
    flip_detect_t f;
    flip_init(&f);
    uint32_t now = 0;
    feed(&f, 0, 0, 1000, &now, 200);
    // Face down but |a| = 1.6 g every other sample (handled roughly): restarts the hold.
    int flips = 0;
    for (int i = 0; i < 20; i++) {
        flips += flip_sample(&f, 0, 0, i % 2 ? -1600 : -950, now) ? 1 : 0;
        now += 100;
    }
    EXPECT_EQ(flips, 0);
}
