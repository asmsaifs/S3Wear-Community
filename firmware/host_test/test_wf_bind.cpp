// watchfaces: binding registry (parse, text and value forms, deps), date patterns,
// number formatting, moon phase, and complication view models.
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "wf_bind.h"
#include "wf_comp.h"

namespace {

constexpr time_t kNow = 1791022140; // 2026-10-03 10:09:00 UTC, a Saturday

class WfTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        setenv("TZ", "UTC0", 1);
        tzset();
        wf_data_init(&data);
        wf_ctx_init(&ctx, &data, kNow, true, true);
    }

    std::string text(const char *name)
    {
        wf_bind_t b;
        EXPECT_TRUE(wf_bind_parse(name, &b)) << name;
        char buf[64];
        wf_ctx_init(&ctx, &data, ctx.now, ctx.h24, ctx.time_valid);
        wf_bind_text(&b, &ctx, buf, sizeof buf);
        return buf;
    }

    bool value(const char *name, int32_t *out)
    {
        wf_bind_t b;
        EXPECT_TRUE(wf_bind_parse(name, &b)) << name;
        wf_ctx_init(&ctx, &data, ctx.now, ctx.h24, ctx.time_valid);
        return wf_bind_value(&b, &ctx, out);
    }

    wf_comp_view_t render(wf_comp_t c)
    {
        wf_ctx_init(&ctx, &data, ctx.now, ctx.h24, ctx.time_valid);
        wf_comp_view_t v;
        wf_comp_render(c, &ctx, &v);
        return v;
    }

    wf_data_t data;
    wf_ctx_t ctx;
};

TEST_F(WfTest, ParseKnownAndUnknown)
{
    wf_bind_t b;
    EXPECT_TRUE(wf_bind_parse("time.hh:mm", &b));
    EXPECT_TRUE(wf_bind_parse("steps.goal_ratio", &b));
    EXPECT_TRUE(wf_bind_parse("date.EEE d MMM", &b));
    EXPECT_STREQ(b.pattern, "EEE d MMM");
    EXPECT_FALSE(wf_bind_parse("time.HH:mm", &b));
    EXPECT_FALSE(wf_bind_parse("steps", &b));
    EXPECT_FALSE(wf_bind_parse("", &b));
    EXPECT_FALSE(wf_bind_parse(nullptr, &b));
    EXPECT_FALSE(wf_bind_parse("date.", &b));
    EXPECT_FALSE(wf_bind_parse("date.EE", &b));    // invalid run length
    EXPECT_FALSE(wf_bind_parse("date.d Q", &b));   // unknown letter
    EXPECT_FALSE(wf_bind_parse("date.yyy", &b));
    EXPECT_FALSE(wf_bind_parse("date.EEEE, d MMMM yyyy (dd/MM)", &b)); // pattern too long
}

TEST_F(WfTest, TimeTextAndFormats)
{
    EXPECT_EQ(text("time.hh:mm"), "10:09");
    EXPECT_EQ(text("time.hh"), "10");
    EXPECT_EQ(text("time.mm"), "09");
    EXPECT_EQ(text("time.ss"), "00");
    EXPECT_EQ(text("time.ampm"), "");
    ctx.h24 = false;
    ctx.now = kNow + 3 * 3600 + 5; // 13:09:05
    EXPECT_EQ(text("time.hh:mm"), "1:09");
    EXPECT_EQ(text("time.hh"), "1");
    EXPECT_EQ(text("time.ss"), "05");
    EXPECT_EQ(text("time.ampm"), "PM");
    ctx.now = kNow - 10 * 3600 - 9 * 60; // 00:00
    EXPECT_EQ(text("time.hh:mm"), "12:00");
    EXPECT_EQ(text("time.ampm"), "AM");
}

TEST_F(WfTest, TimeUnknown)
{
    ctx.time_valid = false;
    EXPECT_EQ(text("time.hh:mm"), "--:--");
    EXPECT_EQ(text("time.mm"), "--");
    EXPECT_EQ(text("date.EEE d MMM"), "--");
    int32_t v = 0;
    EXPECT_FALSE(value("time.hour", &v));
    // Data bindings do not depend on the clock.
    data.battery_pct = 50;
    EXPECT_EQ(text("battery.percent"), "50");
}

TEST_F(WfTest, DatePatterns)
{
    EXPECT_EQ(text("date.EEE d MMM"), "Sat 3 Oct");
    EXPECT_EQ(text("date.EEEE dd MMMM"), "Saturday 03 October");
    EXPECT_EQ(text("date.yyyy-MM-dd"), "2026-10-03");
    EXPECT_EQ(text("date.d/M/yy"), "3/10/26");
    char buf[8];
    struct tm tm = {};
    tm.tm_mday = 3;
    tm.tm_wday = 3;
    EXPECT_TRUE(wf_format_date("EEEE d", &tm, buf, sizeof buf)); // truncated, terminated
    EXPECT_STREQ(buf, "Wednesd");
}

TEST_F(WfTest, HandAngles)
{
    int32_t v = 0;
    ctx.now = kNow + 30; // 10:09:30
    ASSERT_TRUE(value("time.second", &v));
    EXPECT_EQ(v, 1800); // 180.0°
    ASSERT_TRUE(value("time.minute", &v));
    EXPECT_EQ(v, 9 * 60 + 30); // 57.0°
    ASSERT_TRUE(value("time.hour", &v));
    EXPECT_EQ(v, (10 * 3600 + 9 * 60 + 30) / 12); // 304.75° -> 3047
    ctx.now = kNow + 2 * 3600 - 9 * 60; // 12:00 -> 0
    ASSERT_TRUE(value("time.hour", &v));
    EXPECT_EQ(v, 0);
}

TEST_F(WfTest, DataBindings)
{
    int32_t v = 0;
    EXPECT_EQ(text("battery.percent"), "--");
    EXPECT_FALSE(value("battery.ratio", &v));
    data.battery_pct = 82;
    data.charging = true;
    EXPECT_EQ(text("battery.ratio"), "82%");
    ASSERT_TRUE(value("battery.ratio", &v));
    EXPECT_EQ(v, 820);
    EXPECT_EQ(text("battery.charging"), "\xEF\x83\xA7");

    data.steps = 12345;
    EXPECT_EQ(text("steps.count"), "12,345");
    ASSERT_TRUE(value("steps.goal_ratio", &v));
    EXPECT_EQ(v, 1000); // capped
    EXPECT_EQ(text("steps.goal"), "10,000");

    data.weather_valid = true;
    data.temp_c = -4;
    data.weather = WF_WEATHER_SNOW;
    EXPECT_EQ(text("weather.temp"), "-4\xC2\xB0");
    EXPECT_EQ(text("weather.condition"), "Snow");

    EXPECT_EQ(text("next_event.title"), "--");
    EXPECT_EQ(text("next_event.time"), "--:--");
    data.event_valid = true;
    std::strcpy(data.event_title, "Standup");
    data.event_start = kNow + 21 * 60; // 10:30
    EXPECT_EQ(text("next_event.title"), "Standup");
    EXPECT_EQ(text("next_event.time"), "10:30");
}

TEST_F(WfTest, Deps)
{
    wf_bind_t b;
    ASSERT_TRUE(wf_bind_parse("time.second", &b));
    EXPECT_TRUE(wf_bind_deps(&b) & WF_DATA_SECOND);
    ASSERT_TRUE(wf_bind_parse("time.hh:mm", &b));
    EXPECT_EQ(wf_bind_deps(&b), (uint32_t)WF_DATA_TIME);
    ASSERT_TRUE(wf_bind_parse("steps.count", &b));
    EXPECT_EQ(wf_bind_deps(&b), (uint32_t)WF_DATA_STEPS);
}

TEST(WfFormat, Thousands)
{
    char buf[16];
    wf_format_thousands(0, buf, sizeof buf);
    EXPECT_STREQ(buf, "0");
    wf_format_thousands(999, buf, sizeof buf);
    EXPECT_STREQ(buf, "999");
    wf_format_thousands(1000, buf, sizeof buf);
    EXPECT_STREQ(buf, "1,000");
    wf_format_thousands(-1234567, buf, sizeof buf);
    EXPECT_STREQ(buf, "-1,234,567");
    wf_format_thousands(1234567, buf, 4); // truncated, terminated
    EXPECT_STREQ(buf, "1,2");
}

TEST(WfMoon, KnownPhases)
{
    // Mean phases are within about a day of the true ones (35/1000 of a cycle).
    EXPECT_LE(wf_moon_cycle(947182440), 1);           // reference new moon 2000-01-06 18:14
    const int32_t new_2024 = wf_moon_cycle(1704974220); // new moon 2024-01-11 11:57 UTC
    EXPECT_TRUE(new_2024 < 35 || new_2024 > 965) << new_2024;
    EXPECT_NEAR(wf_moon_cycle(1706205240), 500, 35);  // full moon 2024-01-25 17:54 UTC
    EXPECT_STREQ(wf_moon_phase_name(wf_moon_cycle(1706205240)), "Full moon");
    EXPECT_STREQ(wf_moon_phase_name(0), "New moon");
    EXPECT_STREQ(wf_moon_phase_name(990), "New moon");
    EXPECT_STREQ(wf_moon_phase_name(250), "First quarter");
    EXPECT_EQ(wf_moon_illumination(0), 0);
    EXPECT_EQ(wf_moon_illumination(500), 1000);
    EXPECT_EQ(wf_moon_illumination(250), 500);
    EXPECT_GE(wf_moon_cycle(0), 0); // before the reference
}

// --- Complications -----------------------------------------------------------------------

TEST_F(WfTest, CompRegistry)
{
    for (int c = 0; c < WF_COMP_COUNT; c++) {
        const wf_comp_info_t *info = wf_comp_info(static_cast<wf_comp_t>(c));
        ASSERT_NE(info, nullptr);
        EXPECT_EQ(wf_comp_find(info->id), c) << info->id;
        // Every complication's caption fits the 120 px circle (at most 5 letters).
        wf_comp_view_t v = render(static_cast<wf_comp_t>(c));
        EXPECT_LE(std::strlen(v.label), 5u) << info->id;
    }
    EXPECT_EQ(wf_comp_find("nope"), WF_COMP_COUNT);
    EXPECT_EQ(wf_comp_info(WF_COMP_COUNT), nullptr);
}

TEST_F(WfTest, CompUnknownData)
{
    const wf_comp_t unknown[] = {WF_COMP_BATTERY, WF_COMP_STEPS, WF_COMP_WEATHER, WF_COMP_NEXT_EVENT,
                                 WF_COMP_SUNRISE, WF_COMP_WORLD_TIME, WF_COMP_PHONE_BATTERY, WF_COMP_HEART_RATE,
                                 WF_COMP_NOTIFICATIONS};
    for (wf_comp_t c : unknown) {
        const wf_comp_view_t v = render(c);
        EXPECT_FALSE(v.known) << wf_comp_info(c)->id;
        EXPECT_STREQ(v.value, "--") << wf_comp_info(c)->id;
        EXPECT_EQ(v.ratio, -1) << wf_comp_info(c)->id;
    }
    EXPECT_STREQ(render(WF_COMP_ALARM).value, "Off");
    EXPECT_STREQ(render(WF_COMP_TIMER).value, "Off");
    const wf_comp_view_t none = render(WF_COMP_NONE);
    EXPECT_STREQ(none.value, "");
}

TEST_F(WfTest, CompBattery)
{
    data.battery_pct = 80;
    wf_comp_view_t v = render(WF_COMP_BATTERY);
    EXPECT_TRUE(v.known);
    EXPECT_STREQ(v.value, "80%");
    EXPECT_EQ(v.ratio, 800);
    EXPECT_EQ(v.color, 0x30D158u);
    data.battery_pct = 15;
    EXPECT_EQ(render(WF_COMP_BATTERY).color, 0xFFD60Au);
    data.battery_pct = 9;
    EXPECT_EQ(render(WF_COMP_BATTERY).color, 0xFF453Au);
    data.charging = true;
    v = render(WF_COMP_BATTERY);
    EXPECT_EQ(v.color, 0x30D158u);
    EXPECT_STREQ(v.detail, "Charging");
}

TEST_F(WfTest, CompDateAndTime)
{
    wf_comp_view_t v = render(WF_COMP_DATE);
    EXPECT_STREQ(v.value, "3");
    EXPECT_STREQ(v.label, "SAT");
    EXPECT_STREQ(v.detail, "Saturday 3 October");
    ctx.time_valid = false;
    v = render(WF_COMP_DATE);
    EXPECT_FALSE(v.known);
    EXPECT_STREQ(v.value, "--");
}

TEST_F(WfTest, CompSunriseSunset)
{
    data.sunrise_min = 6 * 60 + 12;
    data.sunset_min = 18 * 60 + 5;
    wf_comp_view_t v = render(WF_COMP_SUNRISE); // 10:09: next is sunset
    EXPECT_STREQ(v.value, "18:05");
    EXPECT_STREQ(v.label, "SET");
    ctx.now = kNow - 5 * 3600; // 05:09
    EXPECT_STREQ(render(WF_COMP_SUNRISE).label, "RISE");
    ctx.now = kNow + 9 * 3600; // 19:09: tomorrow's sunrise
    v = render(WF_COMP_SUNRISE);
    EXPECT_STREQ(v.value, "06:12");
    EXPECT_STREQ(v.label, "RISE");
}

TEST_F(WfTest, CompWorldTimeAndTimer)
{
    data.world_valid = true;
    std::strcpy(data.world_label, "TOKYO");
    data.world_utc_offset_s = 9 * 3600;
    wf_comp_view_t v = render(WF_COMP_WORLD_TIME);
    EXPECT_STREQ(v.value, "19:09");
    EXPECT_STREQ(v.label, "TOKYO");

    data.timer_end = kNow + 5 * 60 + 1; // rounds up to 6 min
    EXPECT_STREQ(render(WF_COMP_TIMER).value, "6m");
    data.timer_end = kNow + 75 * 60;
    EXPECT_STREQ(render(WF_COMP_TIMER).value, "1:15");
    data.timer_end = kNow - 1; // finished
    EXPECT_STREQ(render(WF_COMP_TIMER).value, "Off");
}

TEST_F(WfTest, CompStepsAndMoon)
{
    data.steps = 6420;
    wf_comp_view_t v = render(WF_COMP_STEPS);
    EXPECT_STREQ(v.value, "6,420");
    EXPECT_EQ(v.ratio, 642);
    v = render(WF_COMP_MOON); // 2026-10-03: last quarter on 2026-10-03
    EXPECT_STREQ(v.detail, "Last quarter");
    EXPECT_TRUE(v.known);
    EXPECT_GE(v.ratio, 0);
}

} // namespace
