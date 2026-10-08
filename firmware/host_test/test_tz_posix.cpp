// tz_posix: POSIX TZ parsing, DST transitions (both hemispheres, odd offsets, rule
// forms), and agreement with the host C library's own POSIX TZ handling.
#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>
#include <string>

#include "tz_posix.h"

namespace {

// Unix seconds for a UTC date and time (2026-03-08 07:00:00 -> 1772953200).
int64_t utc(int y, int mo, int d, int h = 0, int mi = 0, int s = 0)
{
    struct tm tm = {};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = s;
    // Days from civil via the library under test would be circular; use the classic formula.
    const int a = (14 - (tm.tm_mon + 1)) / 12;
    const int64_t yy = y + 4800 - a;
    const int64_t mm = (tm.tm_mon + 1) + 12 * a - 3;
    const int64_t jdn = d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
    return (jdn - 2440588) * 86400 + h * 3600 + mi * 60 + s;
}

tz_posix_t parse(const char *s)
{
    tz_posix_t tz;
    EXPECT_TRUE(tz_posix_parse(s, &tz)) << s;
    return tz;
}

struct Local {
    int y, mo, d, h, mi, s, isdst;
};

Local local(const tz_posix_t &tz, int64_t t)
{
    struct tm tm;
    tz_posix_localtime(&tz, t, &tm);
    return {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_isdst};
}

#define EXPECT_LOCAL(tz, t, Y, MO, D, H, MI, S, DST)                                    \
    do {                                                                                \
        const Local l_ = local(tz, t);                                                  \
        EXPECT_EQ(l_.y, Y);                                                             \
        EXPECT_EQ(l_.mo, MO);                                                           \
        EXPECT_EQ(l_.d, D);                                                             \
        EXPECT_EQ(l_.h, H);                                                             \
        EXPECT_EQ(l_.mi, MI);                                                           \
        EXPECT_EQ(l_.s, S);                                                             \
        EXPECT_EQ(l_.isdst, DST);                                                       \
    } while (0)

} // namespace

TEST(TzPosix, ParsesFields)
{
    const tz_posix_t tz = parse("CET-1CEST,M3.5.0,M10.5.0/3");
    EXPECT_STREQ(tz.std_name, "CET");
    EXPECT_STREQ(tz.dst_name, "CEST");
    EXPECT_EQ(tz.std_offset_s, 3600);
    EXPECT_EQ(tz.dst_offset_s, 7200);
    EXPECT_TRUE(tz.has_dst);
    EXPECT_EQ(tz.start.kind, TZ_RULE_MWD);
    EXPECT_EQ(tz.start.month, 3);
    EXPECT_EQ(tz.start.week, 5);
    EXPECT_EQ(tz.start.wday, 0);
    EXPECT_EQ(tz.start.time_s, 2 * 3600);
    EXPECT_EQ(tz.end.month, 10);
    EXPECT_EQ(tz.end.time_s, 3 * 3600);
}

TEST(TzPosix, QuotedNamesAndOddOffsets)
{
    const tz_posix_t dhaka = parse("<+06>-6");
    EXPECT_STREQ(dhaka.std_name, "+06");
    EXPECT_EQ(dhaka.std_offset_s, 6 * 3600);
    EXPECT_FALSE(dhaka.has_dst);

    EXPECT_EQ(parse("<+0545>-5:45").std_offset_s, 5 * 3600 + 45 * 60);
    EXPECT_EQ(parse("IST-5:30").std_offset_s, 5 * 3600 + 30 * 60);
    EXPECT_EQ(parse("<-0330>3:30").std_offset_s, -(3 * 3600 + 30 * 60));
    EXPECT_EQ(parse("XXX-5:45:30").std_offset_s, 5 * 3600 + 45 * 60 + 30);
    EXPECT_EQ(parse("UTC0").std_offset_s, 0);
    EXPECT_EQ(parse("UTC+0").std_offset_s, 0);
    EXPECT_EQ(parse("<-12>12").std_offset_s, -12 * 3600);
    EXPECT_EQ(parse("<+14>-14").std_offset_s, 14 * 3600);
}

TEST(TzPosix, RejectsMalformed)
{
    const char *bad[] = {
        "",
        "UT",                           // name < 3 chars
        "EST",                          // no offset
        "ABCDEFGHIJKLMNOP5",            // name > 15 chars
        "<+06-6",                       // unterminated <
        "<+0>-6",                       // quoted name < 3 chars
        "<+06>",                        // no offset
        "EST25",                        // hour > 24
        "EST5:60",                      // minute > 59
        "EST5EDT,M3.2.0",               // one rule only
        "EST5EDT,M13.2.0,M11.1.0",      // month 13
        "EST5EDT,M3.6.0,M11.1.0",       // week 6
        "EST5EDT,M3.0.0,M11.1.0",       // week 0
        "EST5EDT,M3.2.7,M11.1.0",       // weekday 7
        "EST5EDT,M3.2.0/168,M11.1.0",   // time > 167 h
        "EST5EDT,J0,J300",              // J is 1-based
        "EST5EDT,J366,J300",
        "EST5EDT,366,300",              // n is 0..365
        "EST5EDT,M3.2.0,M11.1.0junk",   // trailing text
        "EST5 ",                        // trailing space
        "EST5EDT4,",                    // empty rules
        ":Europe/Paris",                // tz database names are not supported
        "5EST",
    };
    for (const char *s : bad) {
        tz_posix_t tz;
        EXPECT_FALSE(tz_posix_parse(s, &tz)) << '"' << s << '"';
    }
    tz_posix_t tz;
    EXPECT_FALSE(tz_posix_parse(nullptr, &tz));
}

TEST(TzPosix, NoDstZoneNeverChanges)
{
    const tz_posix_t tz = parse("<+06>-6");
    for (int64_t t = utc(2026, 1, 1); t < utc(2027, 1, 1); t += 3600 * 7) {
        bool dst = true;
        EXPECT_EQ(tz_posix_offset(&tz, t, &dst), 6 * 3600);
        EXPECT_FALSE(dst);
    }
    EXPECT_LOCAL(tz, utc(2026, 10, 3, 18, 30), 2026, 10, 4, 0, 30, 0, 0);
}

// US Eastern 2026: DST from Sun 8 Mar 02:00 EST to Sun 1 Nov 02:00 EDT.
TEST(TzPosix, UsEasternTransitions)
{
    const tz_posix_t tz = parse("EST5EDT,M3.2.0,M11.1.0");
    // Spring forward: 01:59:59 EST is followed by 03:00:00 EDT.
    EXPECT_LOCAL(tz, utc(2026, 3, 8, 6, 59, 59), 2026, 3, 8, 1, 59, 59, 0);
    EXPECT_LOCAL(tz, utc(2026, 3, 8, 7, 0, 0), 2026, 3, 8, 3, 0, 0, 1);
    // Fall back: 01:59:59 EDT is followed by 01:00:00 EST (the hour repeats).
    EXPECT_LOCAL(tz, utc(2026, 11, 1, 5, 59, 59), 2026, 11, 1, 1, 59, 59, 1);
    EXPECT_LOCAL(tz, utc(2026, 11, 1, 6, 0, 0), 2026, 11, 1, 1, 0, 0, 0);

    int64_t start = 0;
    int64_t end = 0;
    ASSERT_TRUE(tz_posix_transitions(&tz, 2026, &start, &end));
    EXPECT_EQ(start, utc(2026, 3, 8, 7));
    EXPECT_EQ(end, utc(2026, 11, 1, 6));
}

TEST(TzPosix, DstWithoutRulesUsesUsRules)
{
    const tz_posix_t a = parse("EST5EDT");
    const tz_posix_t b = parse("EST5EDT,M3.2.0,M11.1.0");
    for (int64_t t = utc(2025, 1, 1); t < utc(2028, 1, 1); t += 3600 * 5) {
        ASSERT_EQ(tz_posix_offset(&a, t, nullptr), tz_posix_offset(&b, t, nullptr)) << t;
    }
}

// EU: last Sunday of March 01:00 UTC to last Sunday of October 01:00 UTC.
TEST(TzPosix, CentralEuropeTransitions)
{
    const tz_posix_t tz = parse("CET-1CEST,M3.5.0,M10.5.0/3");
    EXPECT_LOCAL(tz, utc(2026, 3, 29, 0, 59, 59), 2026, 3, 29, 1, 59, 59, 0);
    EXPECT_LOCAL(tz, utc(2026, 3, 29, 1, 0, 0), 2026, 3, 29, 3, 0, 0, 1);
    EXPECT_LOCAL(tz, utc(2026, 10, 25, 0, 59, 59), 2026, 10, 25, 2, 59, 59, 1);
    EXPECT_LOCAL(tz, utc(2026, 10, 25, 1, 0, 0), 2026, 10, 25, 2, 0, 0, 0);
    // "Week 5" means the last one: March 2027 has four Sundays after the 1st... the 28th.
    int64_t start = 0;
    int64_t end = 0;
    ASSERT_TRUE(tz_posix_transitions(&tz, 2027, &start, &end));
    EXPECT_EQ(start, utc(2027, 3, 28, 1));
    EXPECT_EQ(end, utc(2027, 10, 31, 1));
}

TEST(TzPosix, UkTransitionsAtOneUtc)
{
    const tz_posix_t tz = parse("GMT0BST,M3.5.0/1,M10.5.0");
    EXPECT_LOCAL(tz, utc(2026, 3, 29, 0, 59, 59), 2026, 3, 29, 0, 59, 59, 0);
    EXPECT_LOCAL(tz, utc(2026, 3, 29, 1, 0, 0), 2026, 3, 29, 2, 0, 0, 1);
    EXPECT_LOCAL(tz, utc(2026, 10, 25, 1, 0, 0), 2026, 10, 25, 1, 0, 0, 0);
}

// Southern hemisphere: DST spans the new year (Sydney: first Sunday of October to
// first Sunday of April 03:00 AEDT).
TEST(TzPosix, SydneySouthernHemisphere)
{
    const tz_posix_t tz = parse("AEST-10AEDT,M10.1.0,M4.1.0/3");
    EXPECT_LOCAL(tz, utc(2026, 1, 15, 0, 0, 0), 2026, 1, 15, 11, 0, 0, 1);  // summer
    EXPECT_LOCAL(tz, utc(2026, 7, 1, 0, 0, 0), 2026, 7, 1, 10, 0, 0, 0);    // winter
    // End: Sun 5 Apr 2026 03:00 AEDT = 4 Apr 16:00 UTC -> 02:00 AEST.
    EXPECT_LOCAL(tz, utc(2026, 4, 4, 15, 59, 59), 2026, 4, 5, 2, 59, 59, 1);
    EXPECT_LOCAL(tz, utc(2026, 4, 4, 16, 0, 0), 2026, 4, 5, 2, 0, 0, 0);
    // Start: Sun 4 Oct 2026 02:00 AEST = 3 Oct 16:00 UTC -> 03:00 AEDT.
    EXPECT_LOCAL(tz, utc(2026, 10, 3, 15, 59, 59), 2026, 10, 4, 1, 59, 59, 0);
    EXPECT_LOCAL(tz, utc(2026, 10, 3, 16, 0, 0), 2026, 10, 4, 3, 0, 0, 1);
    // Across the new year: still DST on both sides.
    EXPECT_LOCAL(tz, utc(2026, 12, 31, 12, 59, 59), 2026, 12, 31, 23, 59, 59, 1);
    EXPECT_LOCAL(tz, utc(2026, 12, 31, 13, 0, 0), 2027, 1, 1, 0, 0, 0, 1);
}

// Lord Howe: half-hour DST shift, quoted numeric names.
TEST(TzPosix, LordHoweHalfHourDst)
{
    const tz_posix_t tz = parse("<+1030>-10:30<+11>-11,M10.1.0,M4.1.0");
    EXPECT_EQ(tz.dst_offset_s - tz.std_offset_s, 1800);
    // Start: Sun 4 Oct 2026 02:00 +1030 = 3 Oct 15:30 UTC -> 02:30 +11.
    EXPECT_LOCAL(tz, utc(2026, 10, 3, 15, 29, 59), 2026, 10, 4, 1, 59, 59, 0);
    EXPECT_LOCAL(tz, utc(2026, 10, 3, 15, 30, 0), 2026, 10, 4, 2, 30, 0, 1);
    // End: Sun 5 Apr 2026 02:00 +11 = 4 Apr 15:00 UTC -> 01:30 +1030.
    EXPECT_LOCAL(tz, utc(2026, 4, 4, 15, 0, 0), 2026, 4, 5, 1, 30, 0, 0);
}

// Chile: transitions at 24:00 (a rule time past midnight), southern hemisphere.
TEST(TzPosix, SantiagoTwentyFourHundred)
{
    const tz_posix_t tz = parse("<-04>4<-03>,M9.1.6/24,M4.1.6/24");
    // Start: first Saturday of Sep 2026 is the 5th; 24:00 -04 = Sun 6 Sep 04:00 UTC.
    EXPECT_LOCAL(tz, utc(2026, 9, 6, 3, 59, 59), 2026, 9, 5, 23, 59, 59, 0);
    EXPECT_LOCAL(tz, utc(2026, 9, 6, 4, 0, 0), 2026, 9, 6, 1, 0, 0, 1);
    // End: first Saturday of Apr 2026 is the 4th; 24:00 -03 = Sun 5 Apr 03:00 UTC.
    EXPECT_LOCAL(tz, utc(2026, 4, 5, 2, 59, 59), 2026, 4, 4, 23, 59, 59, 1);
    EXPECT_LOCAL(tz, utc(2026, 4, 5, 3, 0, 0), 2026, 4, 4, 23, 0, 0, 0);
}

// Greenland: negative rule time (-1 = 23:00 the day before).
TEST(TzPosix, NuukNegativeRuleTime)
{
    const tz_posix_t tz = parse("<-02>2<-01>,M3.5.0/-1,M10.5.0/0");
    // Start: last Sunday of March 2026 is the 29th; -1:00 -02 = Sat 28 Mar 23:00 -02 = 29 Mar 01:00 UTC.
    EXPECT_LOCAL(tz, utc(2026, 3, 29, 0, 59, 59), 2026, 3, 28, 22, 59, 59, 0);
    EXPECT_LOCAL(tz, utc(2026, 3, 29, 1, 0, 0), 2026, 3, 29, 0, 0, 0, 1);
    // End: Sun 25 Oct 00:00 -01 = 01:00 UTC -> Sat 24 Oct 23:00 -02.
    EXPECT_LOCAL(tz, utc(2026, 10, 25, 1, 0, 0), 2026, 10, 24, 23, 0, 0, 0);
}

// Jn skips Feb 29; n counts it. J60 is always 1 March; zero-based 59 is Feb 29 in a leap year.
TEST(TzPosix, JulianRuleForms)
{
    const tz_posix_t j1 = parse("AAA0BBB,J60/0,J300/0");
    const tz_posix_t j0 = parse("AAA0BBB,59/0,J300/0");
    int64_t s = 0;
    int64_t e = 0;
    ASSERT_TRUE(tz_posix_transitions(&j1, 2028, &s, &e));
    EXPECT_EQ(s, utc(2028, 3, 1));
    ASSERT_TRUE(tz_posix_transitions(&j1, 2027, &s, &e));
    EXPECT_EQ(s, utc(2027, 3, 1));
    ASSERT_TRUE(tz_posix_transitions(&j0, 2028, &s, &e));
    EXPECT_EQ(s, utc(2028, 2, 29));
    ASSERT_TRUE(tz_posix_transitions(&j0, 2027, &s, &e));
    EXPECT_EQ(s, utc(2027, 3, 1));
}

// Permanent DST written the way zic does it (DST from Jan 1 to past Dec 31).
TEST(TzPosix, AllYearDst)
{
    const tz_posix_t tz = parse("EST5EDT,0/0,J365/25");
    for (int64_t t = utc(2026, 1, 1); t < utc(2029, 1, 1); t += 3600 * 11) {
        bool dst = false;
        EXPECT_EQ(tz_posix_offset(&tz, t, &dst), -4 * 3600) << t;
        EXPECT_TRUE(dst) << t;
    }
}

TEST(TzPosix, ExplicitDstOffset)
{
    const tz_posix_t tz = parse("AAA3BBB1,M3.2.0,M11.1.0"); // DST two hours ahead
    EXPECT_EQ(tz.std_offset_s, -3 * 3600);
    EXPECT_EQ(tz.dst_offset_s, -1 * 3600);
    EXPECT_EQ(tz_posix_offset(&tz, utc(2026, 7, 1), nullptr), -3600);
}

TEST(TzPosix, GmtimeMatchesCalendar)
{
    struct tm tm;
    tz_posix_gmtime(utc(2028, 2, 29, 23, 59, 59), &tm);
    EXPECT_EQ(tm.tm_year, 128);
    EXPECT_EQ(tm.tm_mon, 1);
    EXPECT_EQ(tm.tm_mday, 29);
    EXPECT_EQ(tm.tm_yday, 59);
    EXPECT_EQ(tm.tm_wday, 2); // Tuesday
    tz_posix_gmtime(0, &tm);
    EXPECT_EQ(tm.tm_wday, 4); // Thursday
    tz_posix_gmtime(-1, &tm);
    EXPECT_EQ(tm.tm_year, 69);
    EXPECT_EQ(tm.tm_mday, 31);
    EXPECT_EQ(tm.tm_hour, 23);
    EXPECT_EQ(tm.tm_wday, 3);
}

TEST(TzPosix, NextTransition)
{
    const tz_posix_t eu = parse("CET-1CEST,M3.5.0,M10.5.0/3");
    EXPECT_EQ(tz_posix_next_transition(&eu, utc(2026, 1, 10)), utc(2026, 3, 29, 1));
    EXPECT_EQ(tz_posix_next_transition(&eu, utc(2026, 3, 29, 1)), utc(2026, 10, 25, 1)); // strictly after
    EXPECT_EQ(tz_posix_next_transition(&eu, utc(2026, 11, 1)), utc(2027, 3, 28, 1));
    const tz_posix_t syd = parse("AEST-10AEDT,M10.1.0,M4.1.0/3");
    EXPECT_EQ(tz_posix_next_transition(&syd, utc(2026, 12, 25)), utc(2027, 4, 3, 16));
    const tz_posix_t dhaka = parse("<+06>-6");
    EXPECT_EQ(tz_posix_next_transition(&dhaka, utc(2026, 1, 1)), INT64_MAX);
    const tz_posix_t all_year = parse("EST5EDT,0/0,J365/25");
    EXPECT_EQ(tz_posix_next_transition(&all_year, utc(2026, 6, 1)), INT64_MAX);
}

// svc_time hands newlib only the offset in force; it must parse back to the same offset.
TEST(TzPosix, FixedStringForNewlib)
{
    char buf[TZ_POSIX_FIXED_MAX];
    const tz_posix_t eu = parse("CET-1CEST,M3.5.0,M10.5.0/3");
    tz_posix_fixed_string(&eu, utc(2026, 1, 10), buf, sizeof buf);
    EXPECT_STREQ(buf, "CET-1");
    tz_posix_fixed_string(&eu, utc(2026, 7, 1), buf, sizeof buf);
    EXPECT_STREQ(buf, "CEST-2");
    const tz_posix_t dhaka = parse("<+06>-6");
    tz_posix_fixed_string(&dhaka, 0, buf, sizeof buf);
    EXPECT_STREQ(buf, "<+06>-6");
    const tz_posix_t nst = parse("NST3:30NDT,M3.2.0,M11.1.0");
    tz_posix_fixed_string(&nst, utc(2026, 1, 10), buf, sizeof buf);
    EXPECT_STREQ(buf, "NST3:30");
    tz_posix_fixed_string(&nst, utc(2026, 7, 1), buf, sizeof buf);
    EXPECT_STREQ(buf, "NDT2:30");
    const tz_posix_t odd = parse("XXX-5:45:30");
    tz_posix_fixed_string(&odd, 0, buf, sizeof buf);
    EXPECT_STREQ(buf, "XXX-5:45:30");
    const tz_posix_t longname = parse("ABCDEFGHIJKL-3");
    tz_posix_fixed_string(&longname, 0, buf, sizeof buf);
    EXPECT_STREQ(buf, "LCL-3");
    const tz_posix_t nuuk = parse("<-02>2<-01>,M3.5.0/-1,M10.5.0/0");
    tz_posix_fixed_string(&nuuk, utc(2026, 7, 1), buf, sizeof buf);
    EXPECT_STREQ(buf, "<-01>1");
    for (const char *z : {"CET-1CEST,M3.5.0,M10.5.0/3", "<-02>2<-01>,M3.5.0/-1,M10.5.0/0", "IST-5:30"}) {
        const tz_posix_t tz = parse(z);
        for (int64_t t = utc(2026, 1, 1); t < utc(2027, 1, 1); t += 86400 * 5) {
            tz_posix_fixed_string(&tz, t, buf, sizeof buf);
            const tz_posix_t fixed = parse(buf);
            ASSERT_FALSE(fixed.has_dst) << buf;
            ASSERT_EQ(fixed.std_offset_s, tz_posix_offset(&tz, t, nullptr)) << z << " -> " << buf;
        }
    }
}

TEST(TzPosix, FormatOffset)
{
    char buf[16];
    tz_posix_format_offset(0, buf, sizeof buf);
    EXPECT_STREQ(buf, "UTC");
    tz_posix_format_offset(6 * 3600, buf, sizeof buf);
    EXPECT_STREQ(buf, "UTC+6");
    tz_posix_format_offset(5 * 3600 + 45 * 60, buf, sizeof buf);
    EXPECT_STREQ(buf, "UTC+5:45");
    tz_posix_format_offset(-(3 * 3600 + 30 * 60), buf, sizeof buf);
    EXPECT_STREQ(buf, "UTC-3:30");
}

// The watch's localtime is newlib's; the host's libc implements the same POSIX rules.
// Sweep each zone hour by hour through 2024..2030 and around every transition.
TEST(TzPosix, AgreesWithHostLibc)
{
    const char *zones[] = {
        "EST5EDT,M3.2.0,M11.1.0",
        "CET-1CEST,M3.5.0,M10.5.0/3",
        "GMT0BST,M3.5.0/1,M10.5.0",
        "AEST-10AEDT,M10.1.0,M4.1.0/3",
        "<+1030>-10:30<+11>-11,M10.1.0,M4.1.0",
        "<-04>4<-03>,M9.1.6/24,M4.1.6/24",
        // Not "<-02>2<-01>,M3.5.0/-1,M10.5.0/0": macOS libc (and newlib) reject negative rule times.
        "NZST-12NZDT,M9.5.0,M4.1.0/3",
        "<+06>-6",
        "<+0545>-5:45",
        "IST-5:30",
        "<-0330>3:30",
    };
    const char *old = getenv("TZ");
    const std::string saved = old ? old : "";
    for (const char *z : zones) {
        const tz_posix_t tz = parse(z);
        setenv("TZ", z, 1);
        tzset();
        auto check = [&](int64_t t) {
            const time_t tt = (time_t)t;
            struct tm host;
            localtime_r(&tt, &host);
            const Local l = local(tz, t);
            ASSERT_EQ(l.y, host.tm_year + 1900) << z << " t=" << t;
            ASSERT_EQ(l.mo, host.tm_mon + 1) << z << " t=" << t;
            ASSERT_EQ(l.d, host.tm_mday) << z << " t=" << t;
            ASSERT_EQ(l.h, host.tm_hour) << z << " t=" << t;
            ASSERT_EQ(l.mi, host.tm_min) << z << " t=" << t;
            ASSERT_EQ(l.isdst, host.tm_isdst > 0 ? 1 : 0) << z << " t=" << t;
        };
        for (int64_t t = utc(2024, 1, 1); t < utc(2031, 1, 1); t += 3600) {
            check(t);
        }
        for (int y = 2024; y <= 2030; y++) {
            int64_t s = 0;
            int64_t e = 0;
            if (tz_posix_transitions(&tz, y, &s, &e)) {
                for (int64_t d = -2; d <= 1; d++) {
                    check(s + d);
                    check(e + d);
                }
            }
        }
    }
    if (old) {
        setenv("TZ", saved.c_str(), 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
}
