/* Simulator battery backend (sim_battery.h). */
#include "sim_battery.h"

#include <stdio.h>
#include <string.h>

#include "battery_apps.h"
#include "battery_hist.h"
#include "hal.h"
#include "hal_sim.h"
#include "lvgl.h"
#include "sim_data.h"

/* History clock: "now" is one day after its start, moved by the LVGL tick. */
#define DAY_S 86400u

static battery_hist_t s_hist;
static bool s_saver;

static uint32_t now_s(void)
{
    return DAY_S + lv_tick_get() / 1000u;
}

static void read(battery_info_t *out, void *ctx)
{
    (void)ctx;
    hal_battery_t b = {.percent = -1};
    hal_pmu_read_battery(&b);
    *out = (battery_info_t){
        .percent = b.percent,
        .mv = b.mv,
        .charging = b.charging,
        .vbus = b.vbus,
        .saver = s_saver,
        .minutes = battery_hist_minutes(&s_hist, now_s(), b.percent, b.charging),
    };
    battery_hist_get(&s_hist, now_s(), out->history);
}

static void set_saver(bool on, void *ctx)
{
    (void)ctx;
    sim_battery_set_saver(on);
}

static void watch_only(void *ctx)
{
    (void)ctx;
    printf("battery: watch only\n");
}

void sim_battery_init(void)
{
    /* Yesterday 75 % down to 35 % at 01:00, charged 06:00-08:00 to 100 %, then down to 80 %. */
    battery_hist_init(&s_hist);
    for (uint32_t t = 0; t <= DAY_S; t += 600) {
        const uint32_t h = t / 3600;
        int pct;
        bool chg = false;
        if (h < 15) {
            pct = 75 - (int)(t * 40 / (15 * 3600));
        } else if (h < 20) {
            pct = 35;
        } else if (h < 22) {
            pct = 35 + (int)((t - 20 * 3600) * 65 / (2 * 3600));
            chg = true;
        } else {
            pct = 100 - (int)((t - 22 * 3600) * 20 / (2 * 3600 + 1));
        }
        battery_hist_add(&s_hist, t, pct, chg);
    }
    hal_sim_battery_set(80, false);
    battery_hist_add(&s_hist, DAY_S, 80, false);
    const battery_backend_t be = {.read = read, .set_saver = set_saver, .watch_only = watch_only};
    battery_apps_set_backend(&be);
}

void sim_battery_set(int percent, bool vbus)
{
    hal_sim_battery_set(percent, vbus);
    hal_battery_t b;
    hal_pmu_read_battery(&b);
    battery_hist_add(&s_hist, now_s(), b.percent, b.charging);
    sim_data_battery(); /* face complications */
    battery_apps_changed();
}

bool sim_battery_saver(void)
{
    return s_saver;
}

void sim_battery_set_saver(bool on)
{
    s_saver = on;
    printf("battery: saver %s\n", on ? "on" : "off");
    battery_apps_changed();
}
