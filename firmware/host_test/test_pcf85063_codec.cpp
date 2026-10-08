// PCF85063 register encoding (components/drv_pcf85063/pcf85063_codec.c).
#include <gtest/gtest.h>

#include <cstring>
#include <ctime>

#include "pcf85063_codec.h"

TEST(Pcf85063Bcd, RoundTripsAllTwoDigitValues)
{
    for (int v = 0; v <= 99; v++) {
        const uint8_t bcd = pcf85063_bcd_encode(static_cast<uint8_t>(v));
        ASSERT_TRUE(pcf85063_bcd_valid(bcd)) << v;
        EXPECT_EQ(pcf85063_bcd_decode(bcd), v);
    }
}

TEST(Pcf85063Bcd, KnownEncodings)
{
    EXPECT_EQ(pcf85063_bcd_encode(0), 0x00);
    EXPECT_EQ(pcf85063_bcd_encode(9), 0x09);
    EXPECT_EQ(pcf85063_bcd_encode(10), 0x10);
    EXPECT_EQ(pcf85063_bcd_encode(59), 0x59);
    EXPECT_EQ(pcf85063_bcd_encode(99), 0x99);
    EXPECT_EQ(pcf85063_bcd_encode(150), 0x99); // clamped
}

TEST(Pcf85063Bcd, RejectsNonDecimalNibbles)
{
    EXPECT_FALSE(pcf85063_bcd_valid(0x0A));
    EXPECT_FALSE(pcf85063_bcd_valid(0xA0));
    EXPECT_FALSE(pcf85063_bcd_valid(0xFF));
    EXPECT_TRUE(pcf85063_bcd_valid(0x99));
}

static struct tm make_tm(int year, int mon, int mday, int hour, int min, int sec)
{
    struct tm tm;
    std::memset(&tm, 0, sizeof tm);
    tm.tm_year = year - 1900;
    tm.tm_mon = mon - 1;
    tm.tm_mday = mday;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;
    tm.tm_wday = 0;
    return tm;
}

TEST(Pcf85063Time, EncodesDatasheetLayout)
{
    struct tm tm = make_tm(2026, 10, 2, 23, 59, 58);
    tm.tm_wday = 5; // Friday
    uint8_t r[PCF85063_TIME_REG_COUNT];
    ASSERT_TRUE(pcf85063_time_encode(&tm, r));
    const uint8_t expected[PCF85063_TIME_REG_COUNT] = {0x58, 0x59, 0x23, 0x02, 0x05, 0x10, 0x26};
    EXPECT_EQ(0, std::memcmp(r, expected, sizeof r));
}

TEST(Pcf85063Time, RoundTrip)
{
    struct tm in = make_tm(2099, 12, 31, 0, 0, 0);
    in.tm_wday = 4;
    uint8_t r[PCF85063_TIME_REG_COUNT];
    ASSERT_TRUE(pcf85063_time_encode(&in, r));
    struct tm out;
    bool os = true;
    ASSERT_TRUE(pcf85063_time_decode(r, &out, &os));
    EXPECT_FALSE(os);
    EXPECT_EQ(out.tm_year, in.tm_year);
    EXPECT_EQ(out.tm_mon, in.tm_mon);
    EXPECT_EQ(out.tm_mday, in.tm_mday);
    EXPECT_EQ(out.tm_hour, in.tm_hour);
    EXPECT_EQ(out.tm_min, in.tm_min);
    EXPECT_EQ(out.tm_sec, in.tm_sec);
    EXPECT_EQ(out.tm_wday, in.tm_wday);
}

TEST(Pcf85063Time, DecodeReportsOscillatorStopAndMasksUnusedBits)
{
    // OS flag set; unused high bits set in hours/days/months/weekday.
    const uint8_t r[PCF85063_TIME_REG_COUNT] = {0x80 | 0x30, 0x15, 0xC0 | 0x12, 0xC0 | 0x28, 0xF8 | 0x03, 0xE0 | 0x02, 0x24};
    struct tm tm;
    bool os = false;
    ASSERT_TRUE(pcf85063_time_decode(r, &tm, &os));
    EXPECT_TRUE(os);
    EXPECT_EQ(tm.tm_sec, 30);
    EXPECT_EQ(tm.tm_min, 15);
    EXPECT_EQ(tm.tm_hour, 12);
    EXPECT_EQ(tm.tm_mday, 28);
    EXPECT_EQ(tm.tm_wday, 3);
    EXPECT_EQ(tm.tm_mon, 1);
    EXPECT_EQ(tm.tm_year, 124);
}

TEST(Pcf85063Time, DecodeRejectsGarbage)
{
    struct tm tm;
    const uint8_t bad_bcd[PCF85063_TIME_REG_COUNT] = {0x5A, 0, 0, 0x01, 0, 0x01, 0};
    EXPECT_FALSE(pcf85063_time_decode(bad_bcd, &tm, nullptr));
    const uint8_t bad_month[PCF85063_TIME_REG_COUNT] = {0, 0, 0, 0x01, 0, 0x13, 0};
    EXPECT_FALSE(pcf85063_time_decode(bad_month, &tm, nullptr));
    const uint8_t zero_day[PCF85063_TIME_REG_COUNT] = {0, 0, 0, 0x00, 0, 0x01, 0};
    EXPECT_FALSE(pcf85063_time_decode(zero_day, &tm, nullptr));
    const uint8_t bad_hour[PCF85063_TIME_REG_COUNT] = {0, 0, 0x24, 0x01, 0, 0x01, 0};
    EXPECT_FALSE(pcf85063_time_decode(bad_hour, &tm, nullptr));
}

TEST(Pcf85063Time, EncodeRejectsYearsOutsideChipRange)
{
    uint8_t r[PCF85063_TIME_REG_COUNT];
    struct tm tm = make_tm(1999, 12, 31, 23, 59, 59);
    EXPECT_FALSE(pcf85063_time_encode(&tm, r));
    tm = make_tm(2100, 1, 1, 0, 0, 0);
    EXPECT_FALSE(pcf85063_time_encode(&tm, r));
    tm = make_tm(2000, 1, 1, 0, 0, 0);
    EXPECT_TRUE(pcf85063_time_encode(&tm, r));
}

TEST(Pcf85063Alarm, EnablesSecondToDayAndDisablesWeekday)
{
    const struct tm tm = make_tm(2026, 10, 2, 7, 30, 5);
    uint8_t r[PCF85063_ALARM_REG_COUNT];
    pcf85063_alarm_encode(&tm, r);
    const uint8_t expected[PCF85063_ALARM_REG_COUNT] = {0x05, 0x30, 0x07, 0x02, 0x80};
    EXPECT_EQ(0, std::memcmp(r, expected, sizeof r));
}

TEST(Pcf85063Offset, SignedSevenBitWithModeBit)
{
    bool coarse = true;
    EXPECT_EQ(pcf85063_offset_encode(0, false), 0x00);
    EXPECT_EQ(pcf85063_offset_encode(-1, false), 0x7F);
    EXPECT_EQ(pcf85063_offset_encode(63, true), 0xBF);
    EXPECT_EQ(pcf85063_offset_encode(-64, false), 0x40);
    EXPECT_EQ(pcf85063_offset_encode(-100, false), 0x40); // clamped
    for (int s = -64; s <= 63; s++) {
        const uint8_t reg = pcf85063_offset_encode(static_cast<int8_t>(s), s & 1);
        EXPECT_EQ(pcf85063_offset_decode(reg, &coarse), s);
        EXPECT_EQ(coarse, (s & 1) != 0);
    }
}

TEST(Pcf85063Epoch, MatchesKnownTimestamps)
{
    struct tm tm = make_tm(1970, 1, 1, 0, 0, 0);
    EXPECT_EQ(pcf85063_tm_to_unix(&tm), 0);
    tm = make_tm(2000, 1, 1, 0, 0, 0);
    EXPECT_EQ(pcf85063_tm_to_unix(&tm), 946684800);
    tm = make_tm(2024, 2, 29, 12, 0, 0); // leap day
    EXPECT_EQ(pcf85063_tm_to_unix(&tm), 1709208000);
    tm = make_tm(2099, 12, 31, 23, 59, 59);
    EXPECT_EQ(pcf85063_tm_to_unix(&tm), 4102444799);
}
