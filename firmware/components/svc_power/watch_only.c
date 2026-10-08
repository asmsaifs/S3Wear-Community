#include "watch_only.h"

#define MINUTE_MS 60000

int64_t watch_only_minute(int64_t now_ms)
{
    const int64_t t = now_ms + WATCH_ONLY_EARLY_MS;
    return t >= 0 ? t / MINUTE_MS : (t - (MINUTE_MS - 1)) / MINUTE_MS;
}

uint64_t watch_only_sleep_us(int64_t now_ms, int64_t alarm_s, bool ticks)
{
    int64_t wait_ms = -1;
    if (ticks) {
        wait_ms = (watch_only_minute(now_ms) + 1) * MINUTE_MS + WATCH_ONLY_TICK_LATE_MS - now_ms;
    }
    if (alarm_s > 0) {
        const int64_t until_ms = alarm_s * 1000 - now_ms;
        int64_t lead_ms = until_ms / WATCH_ONLY_ALARM_LEAD_DIV;
        if (lead_ms < WATCH_ONLY_ALARM_LEAD_MIN_S * 1000) {
            lead_ms = WATCH_ONLY_ALARM_LEAD_MIN_S * 1000;
        }
        // Already inside the lead: wake at once (the boot itself takes about a second).
        const int64_t alarm_ms = until_ms - lead_ms > 0 ? until_ms - lead_ms : 1;
        if (wait_ms < 0 || alarm_ms < wait_ms) {
            wait_ms = alarm_ms;
        }
    }
    return wait_ms > 0 ? (uint64_t)wait_ms * 1000u : 0;
}

watch_only_wake_t watch_only_wake_kind(int64_t now_s, int64_t alarm_s)
{
    return alarm_s > 0 && alarm_s - now_s <= WATCH_ONLY_ALARM_BOOT_S ? WATCH_ONLY_WAKE_ALARM : WATCH_ONLY_WAKE_TICK;
}
