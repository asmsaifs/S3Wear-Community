// Stopwatch with laps (docs/03-firmware-features.md F6). Pure logic, no LVGL: built
// by firmware/host_test. Times are a caller-supplied monotonic millisecond clock (the
// LVGL tick); differences are wrap-safe, so runs up to 49 days are measured right.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STOPWATCH_LAPS_MAX 30 // newest kept; older ones drop out (numbers keep counting)

typedef struct {
    bool running;
    uint32_t start_ms;   // clock value when the current run started
    uint32_t banked_ms;  // elapsed before the current run
    uint32_t laps[STOPWATCH_LAPS_MAX]; // elapsed at each lap mark, oldest first
    uint8_t lap_count;   // entries in laps
    uint16_t lap_total;  // laps marked since reset (numbering)
} stopwatch_t;

void stopwatch_reset(stopwatch_t *sw);
void stopwatch_start(stopwatch_t *sw, uint32_t now);
void stopwatch_stop(stopwatch_t *sw, uint32_t now);
uint32_t stopwatch_elapsed(const stopwatch_t *sw, uint32_t now);

/** Mark a lap at now (only while running); false otherwise. */
bool stopwatch_lap(stopwatch_t *sw, uint32_t now);

/** Length of kept lap i (0 = oldest kept). */
uint32_t stopwatch_lap_ms(const stopwatch_t *sw, size_t i);
/** Number shown for kept lap i (1-based, counts dropped laps). */
uint16_t stopwatch_lap_number(const stopwatch_t *sw, size_t i);

/** "MM:SS" under an hour, else "H:MM:SS"; *cs gets the hundredths (may be NULL). */
void stopwatch_format(uint32_t ms, char *buf, size_t len, uint8_t *cs);

#ifdef __cplusplus
}
#endif
