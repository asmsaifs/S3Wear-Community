// timer_set: start/pause/resume/restart/remove, expiry, next end, clock wrap.
#include <gtest/gtest.h>

#include "timer_set.h"

TEST(TimerSet, StartAndExpire)
{
    timer_set_t t;
    timer_set_init(&t);
    const uint8_t a = timer_set_start(&t, 60000, 1000);
    const uint8_t b = timer_set_start(&t, 30000, 2000);
    ASSERT_NE(a, 0);
    ASSERT_NE(b, 0);
    EXPECT_NE(a, b);

    uint32_t end = 0;
    ASSERT_TRUE(timer_set_next_end(&t, &end));
    EXPECT_EQ(end, 32000u);
    EXPECT_EQ(timer_left_ms(timer_set_find(&t, a), 11000), 50000u);

    uint8_t ids[TIMER_MAX];
    EXPECT_EQ(timer_set_expire(&t, 31999, ids, TIMER_MAX), 0u);
    ASSERT_EQ(timer_set_expire(&t, 32000, ids, TIMER_MAX), 1u);
    EXPECT_EQ(ids[0], b);
    EXPECT_EQ(timer_set_find(&t, b)->state, TIMER_DONE);
    EXPECT_EQ(timer_left_ms(timer_set_find(&t, b), 40000), 0u);
    // Done timers are not reported again and do not count for the next end.
    EXPECT_EQ(timer_set_expire(&t, 40000, ids, TIMER_MAX), 0u);
    ASSERT_TRUE(timer_set_next_end(&t, &end));
    EXPECT_EQ(end, 61000u);
}

TEST(TimerSet, PauseResumeRestart)
{
    timer_set_t t;
    timer_set_init(&t);
    const uint8_t a = timer_set_start(&t, 10000, 0);
    EXPECT_TRUE(timer_set_pause(&t, a, 4000));
    EXPECT_FALSE(timer_set_pause(&t, a, 4000));
    EXPECT_EQ(timer_left_ms(timer_set_find(&t, a), 9000), 6000u);
    uint32_t end;
    EXPECT_FALSE(timer_set_next_end(&t, &end));
    uint8_t ids[1];
    EXPECT_EQ(timer_set_expire(&t, 100000, ids, 1), 0u);

    EXPECT_TRUE(timer_set_resume(&t, a, 20000));
    EXPECT_FALSE(timer_set_resume(&t, a, 20000));
    ASSERT_TRUE(timer_set_next_end(&t, &end));
    EXPECT_EQ(end, 26000u);

    EXPECT_EQ(timer_set_expire(&t, 26000, ids, 1), 1u);
    EXPECT_TRUE(timer_set_restart(&t, a, 30000));
    EXPECT_EQ(timer_set_find(&t, a)->state, TIMER_RUNNING);
    EXPECT_EQ(timer_left_ms(timer_set_find(&t, a), 30000), 10000u);
}

TEST(TimerSet, LimitsAndRemove)
{
    timer_set_t t;
    timer_set_init(&t);
    EXPECT_EQ(timer_set_start(&t, 0, 0), 0);
    EXPECT_EQ(timer_set_start(&t, TIMER_DURATION_MAX + 1, 0), 0);
    uint8_t ids[TIMER_MAX];
    for (int i = 0; i < TIMER_MAX; i++) {
        ids[i] = timer_set_start(&t, 1000u * (i + 1), 0);
        ASSERT_NE(ids[i], 0);
    }
    EXPECT_EQ(timer_set_start(&t, 1000, 0), 0);
    EXPECT_TRUE(timer_set_remove(&t, ids[2]));
    EXPECT_FALSE(timer_set_remove(&t, ids[2]));
    EXPECT_EQ(t.count, TIMER_MAX - 1);
    EXPECT_EQ(t.items[2].id, ids[3]); // order kept
    EXPECT_NE(timer_set_start(&t, 1000, 0), 0);
}

TEST(TimerSet, ClockWrap)
{
    timer_set_t t;
    timer_set_init(&t);
    const uint32_t now = 0xFFFFF000u;
    const uint8_t a = timer_set_start(&t, 0x2000, now); // ends after the wrap
    uint32_t end;
    ASSERT_TRUE(timer_set_next_end(&t, &end));
    EXPECT_EQ(end, 0x1000u);
    EXPECT_EQ(timer_left_ms(timer_set_find(&t, a), 0xFFFFFF00u), 0x1100u);
    uint8_t ids[1];
    EXPECT_EQ(timer_set_expire(&t, 0xFFFFFFFFu, ids, 1), 0u);
    EXPECT_EQ(timer_set_expire(&t, 0x1000u, ids, 1), 1u);
}
