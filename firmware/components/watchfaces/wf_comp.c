// Complication registry and view models (wf_comp.h). Pure C.
#include "wf_comp.h"

#include <stdio.h>
#include <string.h>

#include "wf_bind.h"

// Colours from docs/04-ui-ux.md §2 (ui_theme.h is LVGL-side; this file is pure C).
#define C_TEXT    0xFFFFFF
#define C_SUCCESS 0x30D158
#define C_WARNING 0xFFD60A
#define C_DANGER  0xFF453A
#define C_MOVE    0xFA114F
#define C_BLUE    0x3D8BFF
#define C_ORANGE  0xFF9F0A
#define C_TEAL    0x40C8E0
#define C_MOON    0xE5E5EA

// App screen ids are the ones the system apps will register (P3-06..P5). Until an
// app exists, a tap shows a toast (engine).
static const wf_comp_info_t INFO[WF_COMP_COUNT] = {
    [WF_COMP_NONE] = {"none", "None", NULL, 0},
    [WF_COMP_BATTERY] = {"battery", "Battery", "battery", WF_DATA_BATTERY},
    [WF_COMP_STEPS] = {"steps", "Steps", "activity", WF_DATA_STEPS},
    [WF_COMP_DATE] = {"date", "Date", "calendar", WF_DATA_TIME},
    [WF_COMP_WEATHER] = {"weather", "Weather", "weather", WF_DATA_WEATHER},
    [WF_COMP_NEXT_EVENT] = {"next_event", "Next event", "calendar", WF_DATA_EVENT | WF_DATA_TIME},
    [WF_COMP_SUNRISE] = {"sunrise", "Sunrise/sunset", "weather", WF_DATA_SUN | WF_DATA_TIME},
    [WF_COMP_WORLD_TIME] = {"world_time", "World time", "world_clock", WF_DATA_WORLD | WF_DATA_TIME},
    [WF_COMP_PHONE_BATTERY] = {"phone_battery", "Phone battery", NULL, WF_DATA_PHONE},
    [WF_COMP_HEART_RATE] = {"heart_rate", "Heart rate", "heart_rate", WF_DATA_HEART},
    [WF_COMP_ALARM] = {"alarm", "Alarm", "alarms", WF_DATA_ALARM | WF_DATA_TIME},
    [WF_COMP_TIMER] = {"timer", "Timer", "timer", WF_DATA_TIMER | WF_DATA_TIME},
    [WF_COMP_NOTIFICATIONS] = {"notifications", "Notifications", "notifications", WF_DATA_NOTIFY},
    [WF_COMP_MOON] = {"moon", "Moon phase", NULL, WF_DATA_TIME},
};

static const char *const WEATHER_LABEL[WF_WEATHER_COUNT] = {"CLEAR", "CLOUD", "RAIN", "SNOW", "STORM", "FOG"};

const wf_comp_info_t *wf_comp_info(wf_comp_t c)
{
    return (unsigned)c < WF_COMP_COUNT ? &INFO[c] : NULL;
}

wf_comp_t wf_comp_find(const char *id)
{
    for (int c = 0; id && c < WF_COMP_COUNT; c++) {
        if (strcmp(INFO[c].id, id) == 0) {
            return (wf_comp_t)c;
        }
    }
    return WF_COMP_COUNT;
}

static void hm_local(time_t t, bool h24, char *buf, size_t len)
{
    struct tm tm;
    localtime_r(&t, &tm);
    wf_format_hm(tm.tm_hour, tm.tm_min, h24, buf, len);
}

void wf_comp_render(wf_comp_t c, const wf_ctx_t *ctx, wf_comp_view_t *v)
{
    memset(v, 0, sizeof *v);
    v->ratio = -1;
    v->color = C_TEXT;
    if (c == WF_COMP_NONE || (unsigned)c >= WF_COMP_COUNT) {
        return;
    }
    const wf_data_t *d = ctx->data;
    const struct tm *tm = &ctx->tm;
    strcpy(v->value, "--");

    switch (c) {
    case WF_COMP_BATTERY:
        strcpy(v->label, "BATT");
        v->color = C_SUCCESS;
        if (d->battery_pct >= 0) {
            v->known = true;
            snprintf(v->value, sizeof v->value, "%d%%", d->battery_pct);
            snprintf(v->detail, sizeof v->detail, "%s", d->charging ? "Charging" : "Battery");
            v->ratio = (int16_t)(d->battery_pct * 10);
            if (!d->charging) {
                v->color = d->battery_pct <= 10 ? C_DANGER : d->battery_pct <= 20 ? C_WARNING : C_SUCCESS;
            }
        }
        break;
    case WF_COMP_STEPS:
        strcpy(v->label, "STEPS");
        v->color = C_MOVE;
        if (d->steps >= 0) {
            v->known = true;
            wf_format_thousands(d->steps, v->value, sizeof v->value);
            snprintf(v->detail, sizeof v->detail, "steps");
            if (d->steps_goal > 0) {
                const int64_t r = (int64_t)d->steps * 1000 / d->steps_goal;
                v->ratio = (int16_t)(r > 1000 ? 1000 : r);
            }
        }
        break;
    case WF_COMP_DATE:
        if (ctx->time_valid) {
            v->known = true;
            snprintf(v->value, sizeof v->value, "%d", tm->tm_mday);
            wf_format_date("EEE", tm, v->label, sizeof v->label);
            for (char *p = v->label; *p; p++) {
                *p = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
            }
            wf_format_date("EEEE d MMMM", tm, v->detail, sizeof v->detail);
        } else {
            strcpy(v->label, "DATE");
        }
        break;
    case WF_COMP_WEATHER:
        v->color = C_TEAL;
        strcpy(v->label, "TEMP");
        if (d->weather_valid) {
            v->known = true;
            snprintf(v->value, sizeof v->value, "%d\xC2\xB0", d->temp_c);
            if (d->weather < WF_WEATHER_COUNT) {
                strcpy(v->label, WEATHER_LABEL[d->weather]);
            }
            snprintf(v->detail, sizeof v->detail, "%s %d\xC2\xB0/%d\xC2\xB0", wf_weather_name(d->weather),
                     d->temp_lo_c, d->temp_hi_c);
        }
        break;
    case WF_COMP_NEXT_EVENT:
        v->color = C_ORANGE;
        strcpy(v->label, "NEXT");
        if (d->event_valid) {
            v->known = true;
            hm_local(d->event_start, ctx->h24, v->value, sizeof v->value);
            snprintf(v->detail, sizeof v->detail, "%s", d->event_title);
        } else {
            strcpy(v->detail, "No events");
        }
        break;
    case WF_COMP_SUNRISE:
        v->color = C_ORANGE;
        strcpy(v->label, "SUN");
        if (d->sunrise_min >= 0 && d->sunset_min >= 0 && ctx->time_valid) {
            v->known = true;
            const int now_min = tm->tm_hour * 60 + tm->tm_min;
            const bool set = now_min >= d->sunrise_min && now_min < d->sunset_min;
            const int m = set ? d->sunset_min : d->sunrise_min;
            wf_format_hm(m / 60, m % 60, ctx->h24, v->value, sizeof v->value);
            strcpy(v->label, set ? "SET" : "RISE");
            snprintf(v->detail, sizeof v->detail, "%s", set ? "Sunset" : "Sunrise");
        }
        break;
    case WF_COMP_WORLD_TIME:
        v->color = C_TEAL;
        strcpy(v->label, "WORLD");
        if (d->world_valid && ctx->time_valid) {
            v->known = true;
            const time_t t = ctx->now + d->world_utc_offset_s;
            struct tm wt;
            gmtime_r(&t, &wt);
            wf_format_hm(wt.tm_hour, wt.tm_min, ctx->h24, v->value, sizeof v->value);
            snprintf(v->label, sizeof v->label, "%.5s", d->world_label);
            snprintf(v->detail, sizeof v->detail, "%s", d->world_label);
        }
        break;
    case WF_COMP_PHONE_BATTERY:
        v->color = C_SUCCESS;
        strcpy(v->label, "PHONE");
        if (d->phone_battery_pct >= 0) {
            v->known = true;
            snprintf(v->value, sizeof v->value, "%d%%", d->phone_battery_pct);
            snprintf(v->detail, sizeof v->detail, "Phone");
            v->ratio = (int16_t)(d->phone_battery_pct * 10);
        }
        break;
    case WF_COMP_HEART_RATE:
        v->color = C_DANGER;
        strcpy(v->label, "BPM");
        if (d->heart_bpm > 0) {
            v->known = true;
            snprintf(v->value, sizeof v->value, "%u", d->heart_bpm);
            snprintf(v->detail, sizeof v->detail, "bpm");
        }
        break;
    case WF_COMP_ALARM:
        v->color = C_WARNING;
        strcpy(v->label, "ALARM");
        v->known = true;
        if (d->alarm_next > 0) {
            hm_local(d->alarm_next, ctx->h24, v->value, sizeof v->value);
            snprintf(v->detail, sizeof v->detail, "Alarm");
        } else {
            strcpy(v->value, "Off");
            snprintf(v->detail, sizeof v->detail, "No alarm");
        }
        break;
    case WF_COMP_TIMER:
        v->color = C_ORANGE;
        strcpy(v->label, "TIMER");
        v->known = true;
        if (d->timer_end > ctx->now) {
            // Minute resolution: the face updates once a minute (and in AOD).
            const int64_t min = (d->timer_end - ctx->now + 59) / 60;
            if (min >= 60) {
                snprintf(v->value, sizeof v->value, "%d:%02d", (int)(min / 60), (int)(min % 60));
            } else {
                snprintf(v->value, sizeof v->value, "%dm", (int)min);
            }
            snprintf(v->detail, sizeof v->detail, "left");
        } else {
            strcpy(v->value, "Off");
            snprintf(v->detail, sizeof v->detail, "No timer");
        }
        break;
    case WF_COMP_NOTIFICATIONS:
        v->color = C_BLUE;
        strcpy(v->label, "NOTIF");
        if (d->notifications >= 0) {
            v->known = true;
            snprintf(v->value, sizeof v->value, "%d", d->notifications);
            snprintf(v->detail, sizeof v->detail, d->notifications == 1 ? "notification" : "notifications");
        }
        break;
    case WF_COMP_MOON: {
        v->color = C_MOON;
        strcpy(v->label, "MOON");
        v->known = true;
        const int32_t cycle = wf_moon_cycle(ctx->now);
        const int32_t lit = wf_moon_illumination(cycle);
        snprintf(v->value, sizeof v->value, "%ld%%", (long)((lit + 5) / 10));
        snprintf(v->detail, sizeof v->detail, "%s", wf_moon_phase_name(cycle));
        v->ratio = (int16_t)lit;
        break;
    }
    default:
        break;
    }
}
