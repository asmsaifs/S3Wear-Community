#include "pcf85063_codec.h"

#include <string.h>

#define AEN_DISABLED 0x80

uint8_t pcf85063_bcd_encode(uint8_t value)
{
    if (value > 99) {
        value = 99;
    }
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

uint8_t pcf85063_bcd_decode(uint8_t bcd)
{
    return (uint8_t)((bcd >> 4) * 10 + (bcd & 0x0F));
}

bool pcf85063_bcd_valid(uint8_t bcd)
{
    return (bcd >> 4) <= 9 && (bcd & 0x0F) <= 9;
}

static bool field(uint8_t reg, uint8_t mask, int lo, int hi, int *out)
{
    const uint8_t bcd = reg & mask;
    if (!pcf85063_bcd_valid(bcd)) {
        return false;
    }
    const int v = pcf85063_bcd_decode(bcd);
    if (v < lo || v > hi) {
        return false;
    }
    *out = v;
    return true;
}

bool pcf85063_time_decode(const uint8_t regs[PCF85063_TIME_REG_COUNT], struct tm *out, bool *osc_stopped)
{
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    int mon = 0;
    int year = 0;
    if (!field(regs[0], 0x7F, 0, 59, &tm.tm_sec) || !field(regs[1], 0x7F, 0, 59, &tm.tm_min) ||
        !field(regs[2], 0x3F, 0, 23, &tm.tm_hour) || !field(regs[3], 0x3F, 1, 31, &tm.tm_mday) ||
        !field(regs[4], 0x07, 0, 6, &tm.tm_wday) || !field(regs[5], 0x1F, 1, 12, &mon) ||
        !field(regs[6], 0xFF, 0, 99, &year)) {
        return false;
    }
    tm.tm_mon = mon - 1;
    tm.tm_year = year + 100; // years since 1900
    tm.tm_isdst = 0;
    if (osc_stopped) {
        *osc_stopped = (regs[0] & 0x80) != 0;
    }
    *out = tm;
    return true;
}

bool pcf85063_time_encode(const struct tm *tm, uint8_t regs[PCF85063_TIME_REG_COUNT])
{
    const int year = tm->tm_year + 1900;
    if (year < 2000 || year > 2099) {
        return false;
    }
    regs[0] = pcf85063_bcd_encode((uint8_t)tm->tm_sec); // OS = 0
    regs[1] = pcf85063_bcd_encode((uint8_t)tm->tm_min);
    regs[2] = pcf85063_bcd_encode((uint8_t)tm->tm_hour);
    regs[3] = pcf85063_bcd_encode((uint8_t)tm->tm_mday);
    regs[4] = (uint8_t)(tm->tm_wday & 0x07);
    regs[5] = pcf85063_bcd_encode((uint8_t)(tm->tm_mon + 1));
    regs[6] = pcf85063_bcd_encode((uint8_t)(year - 2000));
    return true;
}

void pcf85063_alarm_encode(const struct tm *tm, uint8_t regs[PCF85063_ALARM_REG_COUNT])
{
    regs[0] = pcf85063_bcd_encode((uint8_t)tm->tm_sec);
    regs[1] = pcf85063_bcd_encode((uint8_t)tm->tm_min);
    regs[2] = pcf85063_bcd_encode((uint8_t)tm->tm_hour);
    regs[3] = pcf85063_bcd_encode((uint8_t)tm->tm_mday);
    regs[4] = AEN_DISABLED;
}

uint8_t pcf85063_offset_encode(int8_t steps, bool coarse)
{
    if (steps < -64) {
        steps = -64;
    } else if (steps > 63) {
        steps = 63;
    }
    return (uint8_t)((coarse ? 0x80 : 0x00) | ((uint8_t)steps & 0x7F));
}

int8_t pcf85063_offset_decode(uint8_t reg, bool *coarse)
{
    if (coarse) {
        *coarse = (reg & 0x80) != 0;
    }
    uint8_t v = reg & 0x7F;
    return (int8_t)((v & 0x40) ? (int)v - 128 : (int)v); // sign-extend 7 bits
}

int64_t pcf85063_tm_to_unix(const struct tm *utc)
{
    // Days from civil (H. Hinnant), proleptic Gregorian calendar.
    int64_t y = (int64_t)utc->tm_year + 1900;
    const int64_t m = utc->tm_mon + 1;
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + utc->tm_mday - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = era * 146097 + doe - 719468;
    return days * 86400 + utc->tm_hour * 3600 + utc->tm_min * 60 + utc->tm_sec;
}
