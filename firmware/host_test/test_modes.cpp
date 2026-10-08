// modes: weekly windows (overnight, 24 h, days), next edge, by-hand on/off and
// skipping a scheduled window, what the modes imply (quiet, dark).
#include <gtest/gtest.h>

#include "modes.h"

namespace {

constexpr int SUN = 0;
constexpr int MON = 1;
constexpr int FRI = 5;
constexpr int SAT = 6;

constexpr int hm(int h, int m)
{
    return h * 60 + m;
}

constexpr uint8_t WEEKDAYS = 0x3E; // Mon..Fri
constexpr uint8_t DAILY = 0x7F;

modes_t empty()
{
    modes_t m{};
    return m;
}

} // namespace

TEST(ModeSched, NoDaysIsOff)
{
    const mode_sched_t s{0, static_cast<uint16_t>(hm(22, 0)), static_cast<uint16_t>(hm(7, 0))};
    EXPECT_FALSE(mode_sched_active(&s, MON, hm(23, 0)));
    EXPECT_EQ(mode_sched_next_edge(&s, MON, hm(23, 0)), 0u);
}

TEST(ModeSched, SameDayWindow)
{
    const mode_sched_t s{DAILY, static_cast<uint16_t>(hm(9, 0)), static_cast<uint16_t>(hm(17, 30))};
    EXPECT_FALSE(mode_sched_active(&s, MON, hm(8, 59)));
    EXPECT_TRUE(mode_sched_active(&s, MON, hm(9, 0)));
    EXPECT_TRUE(mode_sched_active(&s, MON, hm(17, 29)));
    EXPECT_FALSE(mode_sched_active(&s, MON, hm(17, 30))); // end is exclusive
    EXPECT_EQ(mode_sched_next_edge(&s, MON, hm(8, 0)), 60u);
    EXPECT_EQ(mode_sched_next_edge(&s, MON, hm(17, 0)), 30u);
}

TEST(ModeSched, OvernightBelongsToItsStartDay)
{
    // Weeknights 22:00-07:00, starting Mon..Fri.
    const mode_sched_t s{WEEKDAYS, static_cast<uint16_t>(hm(22, 0)), static_cast<uint16_t>(hm(7, 0))};
    EXPECT_TRUE(mode_sched_active(&s, MON, hm(22, 0)));
    EXPECT_TRUE(mode_sched_active(&s, MON, hm(23, 59)));
    EXPECT_TRUE(mode_sched_active(&s, 2, hm(6, 59))); // Tuesday morning: Monday's window
    EXPECT_FALSE(mode_sched_active(&s, 2, hm(7, 0)));
    EXPECT_TRUE(mode_sched_active(&s, SAT, hm(3, 0))); // Friday night runs into Saturday
    EXPECT_FALSE(mode_sched_active(&s, SAT, hm(22, 30)));
    EXPECT_FALSE(mode_sched_active(&s, SUN, hm(23, 0)));
    EXPECT_FALSE(mode_sched_active(&s, MON, hm(3, 0))); // Sunday night is not scheduled
    // Saturday 07:00 -> Monday 22:00
    EXPECT_EQ(mode_sched_next_edge(&s, SAT, hm(7, 0)), static_cast<uint32_t>(2 * 1440 + hm(15, 0)));
    // Friday 23:00 -> Saturday 07:00
    EXPECT_EQ(mode_sched_next_edge(&s, FRI, hm(23, 0)), 480u);
}

TEST(ModeSched, EqualStartAndEndIs24h)
{
    const mode_sched_t s{1u << SAT, static_cast<uint16_t>(hm(8, 0)), static_cast<uint16_t>(hm(8, 0))};
    EXPECT_FALSE(mode_sched_active(&s, SAT, hm(7, 59)));
    EXPECT_TRUE(mode_sched_active(&s, SAT, hm(8, 0)));
    EXPECT_TRUE(mode_sched_active(&s, SUN, hm(7, 59)));
    EXPECT_FALSE(mode_sched_active(&s, SUN, hm(8, 0)));
    EXPECT_EQ(mode_sched_next_edge(&s, SAT, hm(8, 0)), 1440u);

    const mode_sched_t always{DAILY, 0, 0};
    EXPECT_TRUE(mode_sched_active(&always, MON, hm(12, 0)));
    EXPECT_EQ(mode_sched_next_edge(&always, MON, hm(12, 0)), 0u); // never changes
}

TEST(ModeSched, UnknownTimeIsOff)
{
    const mode_sched_t s{DAILY, 0, 0};
    EXPECT_FALSE(mode_sched_active(&s, -1, 0));
    EXPECT_EQ(mode_sched_next_edge(&s, -1, 0), 0u);
}

TEST(Modes, ImpliedFlags)
{
    modes_t m = empty();
    modes_state_t st = modes_eval(&m, MON, hm(12, 0));
    EXPECT_FALSE(st.quiet);
    EXPECT_FALSE(st.dark);

    modes_set(&m, MODE_DND, true, MON, hm(12, 0));
    st = modes_eval(&m, MON, hm(12, 0));
    EXPECT_TRUE(st.dnd);
    EXPECT_TRUE(st.quiet);
    EXPECT_FALSE(st.dark); // DND alone: tap and raise still wake

    m = empty();
    modes_set(&m, MODE_SLEEP, true, MON, hm(12, 0));
    st = modes_eval(&m, MON, hm(12, 0));
    EXPECT_FALSE(st.dnd);
    EXPECT_TRUE(st.quiet);
    EXPECT_TRUE(st.dark);

    m = empty();
    modes_set(&m, MODE_THEATER, true, MON, hm(12, 0));
    st = modes_eval(&m, MON, hm(12, 0));
    EXPECT_TRUE(st.theater);
    EXPECT_TRUE(st.quiet);
    EXPECT_TRUE(st.dark);
}

TEST(Modes, ScheduleTurnsOnAndOff)
{
    modes_t m = empty();
    m.sched[MODE_SLEEP] = {DAILY, static_cast<uint16_t>(hm(23, 0)), static_cast<uint16_t>(hm(7, 0))};
    EXPECT_FALSE(modes_eval(&m, MON, hm(22, 59)).sleep);
    EXPECT_TRUE(modes_eval(&m, MON, hm(23, 0)).sleep);
    EXPECT_TRUE(modes_eval(&m, 2, hm(6, 0)).dark);
    EXPECT_FALSE(modes_eval(&m, 2, hm(7, 0)).sleep);
    EXPECT_EQ(modes_next_edge(&m, MON, hm(22, 0)), 60u);
}

TEST(Modes, OffByHandSkipsTheRestOfTheWindow)
{
    modes_t m = empty();
    m.sched[MODE_DND] = {DAILY, static_cast<uint16_t>(hm(22, 0)), static_cast<uint16_t>(hm(7, 0))};
    ASSERT_TRUE(modes_eval(&m, MON, hm(22, 30)).dnd);
    modes_set(&m, MODE_DND, false, MON, hm(22, 30));
    EXPECT_FALSE(modes_eval(&m, MON, hm(23, 0)).dnd);
    EXPECT_FALSE(modes_eval(&m, 2, hm(6, 59)).dnd); // still the same window
    EXPECT_FALSE(modes_eval(&m, 2, hm(7, 0)).dnd);  // window over: skip cleared
    EXPECT_FALSE(m.skip[MODE_DND]);
    EXPECT_TRUE(modes_eval(&m, 2, hm(22, 0)).dnd); // the next window turns it on again
}

TEST(Modes, OnByHandStaysOnAfterTheWindow)
{
    modes_t m = empty();
    m.sched[MODE_DND] = {DAILY, static_cast<uint16_t>(hm(22, 0)), static_cast<uint16_t>(hm(7, 0))};
    modes_set(&m, MODE_DND, true, MON, hm(21, 0));
    EXPECT_TRUE(modes_eval(&m, MON, hm(21, 0)).dnd);
    EXPECT_TRUE(modes_eval(&m, 2, hm(8, 0)).dnd);
    modes_set(&m, MODE_DND, false, 2, hm(8, 0)); // outside a window: no skip
    EXPECT_FALSE(m.skip[MODE_DND]);
    EXPECT_FALSE(modes_eval(&m, 2, hm(8, 0)).dnd);
    EXPECT_TRUE(modes_eval(&m, 2, hm(22, 0)).dnd);
}

TEST(Modes, OnAgainInsideASkippedWindow)
{
    modes_t m = empty();
    m.sched[MODE_SLEEP] = {DAILY, static_cast<uint16_t>(hm(23, 0)), static_cast<uint16_t>(hm(7, 0))};
    modes_set(&m, MODE_SLEEP, false, MON, hm(23, 30));
    EXPECT_FALSE(modes_eval(&m, MON, hm(23, 30)).sleep);
    modes_set(&m, MODE_SLEEP, true, MON, hm(23, 45));
    EXPECT_FALSE(m.skip[MODE_SLEEP]);
    EXPECT_TRUE(modes_eval(&m, MON, hm(23, 45)).sleep);
}

TEST(Modes, SkipEndsWhenTheClockLeavesTheWindow)
{
    modes_t m = empty();
    m.sched[MODE_DND] = {DAILY, static_cast<uint16_t>(hm(22, 0)), static_cast<uint16_t>(hm(7, 0))};
    modes_set(&m, MODE_DND, false, MON, hm(23, 0));
    // Time unknown (clock lost): schedules are ignored, and the skip with them.
    EXPECT_FALSE(modes_eval(&m, -1, 0).dnd);
    EXPECT_FALSE(m.skip[MODE_DND]);
}

TEST(Modes, TheaterHasNoSchedule)
{
    modes_t m = empty();
    m.sched[MODE_THEATER] = {DAILY, 0, 0}; // ignored
    EXPECT_FALSE(modes_eval(&m, MON, hm(12, 0)).theater);
    EXPECT_EQ(modes_next_edge(&m, MON, hm(12, 0)), 0u);
    modes_set(&m, MODE_THEATER, false, MON, hm(12, 0));
    EXPECT_FALSE(m.skip[MODE_THEATER]);
}

TEST(Modes, NextEdgeIsTheEarliest)
{
    modes_t m = empty();
    m.sched[MODE_DND] = {DAILY, static_cast<uint16_t>(hm(22, 0)), static_cast<uint16_t>(hm(7, 0))};
    m.sched[MODE_SLEEP] = {DAILY, static_cast<uint16_t>(hm(23, 0)), static_cast<uint16_t>(hm(6, 30))};
    EXPECT_EQ(modes_next_edge(&m, MON, hm(21, 0)), 60u);
    EXPECT_EQ(modes_next_edge(&m, MON, hm(22, 0)), 60u);
    EXPECT_EQ(modes_next_edge(&m, 2, hm(6, 0)), 30u);
    EXPECT_EQ(modes_next_edge(&m, 2, hm(6, 30)), 30u);
}
