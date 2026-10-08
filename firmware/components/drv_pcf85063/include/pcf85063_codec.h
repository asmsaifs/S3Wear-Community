// PCF85063A register encoding (pure logic, no ESP-IDF: host-tested).
// Time registers 0x04..0x0A hold UTC as BCD; years 00..99 map to 2000..2099.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PCF85063_TIME_REG_COUNT  7 // seconds, minutes, hours, days, weekdays, months, years
#define PCF85063_ALARM_REG_COUNT 5 // second, minute, hour, day, weekday alarms

/** 0..99 -> packed BCD. Values > 99 are clamped to 99. */
uint8_t pcf85063_bcd_encode(uint8_t value);

/** Packed BCD -> 0..99. Caller should check pcf85063_bcd_valid() first. */
uint8_t pcf85063_bcd_decode(uint8_t bcd);

/** True if both nibbles are 0..9. */
bool pcf85063_bcd_valid(uint8_t bcd);

/**
 * Decode registers 0x04..0x0A. *osc_stopped gets the OS flag (seconds bit 7): when
 * set, the clock lost power/oscillation and the time is not trustworthy.
 * Returns false if any field is not valid BCD or out of range.
 */
bool pcf85063_time_decode(const uint8_t regs[PCF85063_TIME_REG_COUNT], struct tm *out, bool *osc_stopped);

/** Encode a UTC struct tm (normalised) into registers 0x04..0x0A; clears OS. false if year not 2000..2099. */
bool pcf85063_time_encode(const struct tm *tm, uint8_t regs[PCF85063_TIME_REG_COUNT]);

/**
 * Alarm registers 0x0B..0x0F for an exact second/minute/hour/day-of-month match;
 * the weekday alarm is disabled (AEN bit 7 = 1 means disabled).
 */
void pcf85063_alarm_encode(const struct tm *tm, uint8_t regs[PCF85063_ALARM_REG_COUNT]);

/** UTC broken-down time -> Unix seconds, independent of TZ (newlib has no timegm). */
int64_t pcf85063_tm_to_unix(const struct tm *utc);

/**
 * Offset register 0x02. steps is a signed 7-bit value (-64..63); coarse = MODE bit:
 * normal mode 4.34 ppm/step applied every 2 h, coarse 4.069 ppm/step every 4 min.
 */
uint8_t pcf85063_offset_encode(int8_t steps, bool coarse);
int8_t pcf85063_offset_decode(uint8_t reg, bool *coarse);

#ifdef __cplusplus
}
#endif
