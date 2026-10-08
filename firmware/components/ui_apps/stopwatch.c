// Stopwatch (stopwatch.h). Pure C.
#include "stopwatch.h"

#include <stdio.h>
#include <string.h>

void stopwatch_reset(stopwatch_t *sw)
{
    memset(sw, 0, sizeof *sw);
}

void stopwatch_start(stopwatch_t *sw, uint32_t now)
{
    if (!sw->running) {
        sw->running = true;
        sw->start_ms = now;
    }
}

void stopwatch_stop(stopwatch_t *sw, uint32_t now)
{
    if (sw->running) {
        sw->banked_ms += now - sw->start_ms;
        sw->running = false;
    }
}

uint32_t stopwatch_elapsed(const stopwatch_t *sw, uint32_t now)
{
    return sw->banked_ms + (sw->running ? now - sw->start_ms : 0);
}

bool stopwatch_lap(stopwatch_t *sw, uint32_t now)
{
    if (!sw->running) {
        return false;
    }
    if (sw->lap_count == STOPWATCH_LAPS_MAX) {
        // Keep the lengths right: the oldest kept mark becomes the base of the next.
        memmove(sw->laps, sw->laps + 1, (STOPWATCH_LAPS_MAX - 1) * sizeof sw->laps[0]);
        sw->lap_count--;
    }
    sw->laps[sw->lap_count++] = stopwatch_elapsed(sw, now);
    sw->lap_total++;
    return true;
}

uint32_t stopwatch_lap_ms(const stopwatch_t *sw, size_t i)
{
    if (i >= sw->lap_count) {
        return 0;
    }
    if (i > 0) {
        return sw->laps[i] - sw->laps[i - 1];
    }
    // The first kept lap: from the previous mark, which dropped out once laps overflowed.
    return sw->lap_total > sw->lap_count ? 0 : sw->laps[0];
}

uint16_t stopwatch_lap_number(const stopwatch_t *sw, size_t i)
{
    return (uint16_t)(sw->lap_total - sw->lap_count + i + 1);
}

void stopwatch_format(uint32_t ms, char *buf, size_t len, uint8_t *cs)
{
    const uint32_t s = ms / 1000;
    if (s >= 3600) {
        snprintf(buf, len, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
                 (unsigned long)(s % 60));
    } else {
        snprintf(buf, len, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    }
    if (cs) {
        *cs = (uint8_t)(ms % 1000 / 10);
    }
}
