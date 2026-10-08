// Console: `pmu [rail <name> on|off [mV] | off]` — AXP2101 bring-up checks (P1-05).
// Also logs every BSP_PMU_EVENT so "USB plugged" etc. show up on the console.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "bsp_s3w.h"
#include "esp_check.h"
#include "esp_console.h"
#include "esp_log.h"
#include "svc_diag_priv.h"

static const char *TAG = "cmd_pmu";

static const char *chg_state_name(axp2101_chg_state_t s)
{
    static const char *const k[] = {"trickle", "pre-charge", "constant current", "constant voltage", "done",
                                    "not charging"};
    return (unsigned)s < sizeof k / sizeof k[0] ? k[s] : "?";
}

static int pmu_print(axp2101_handle_t pmu)
{
    axp2101_status_t st;
    if (axp2101_read_status(pmu, &st) != ESP_OK) {
        printf("AXP2101 read failed\n");
        return 1;
    }
    printf("VBUS      : %s, %u mV\n", st.vbus_good ? "present" : "absent", st.vbus_mv);
    if (st.battery_present) {
        printf("battery   : %d %%, %u mV, %s\n", st.battery_pct, st.vbat_mv,
               st.charging ? "charging" : st.discharging ? "discharging" : "idle");
    } else {
        printf("battery   : absent\n");
    }
    printf("charger   : %s\n", chg_state_name(st.chg_state));
    printf("VSYS      : %u mV\n", st.vsys_mv);
    printf("die temp  : %d.%d C\n", st.die_temp_dc / 10, abs(st.die_temp_dc % 10));

    axp2101_charger_cfg_t cc;
    if (axp2101_charger_get(pmu, &cc) == ESP_OK) {
        printf("charge cfg: CC %u mA, term %u mA, pre %u mA, CV %u mV\n", cc.cc_ma, cc.term_ma, cc.precharge_ma,
               cc.cv_mv);
    }
    axp2101_pkey_off_t off;
    if (axp2101_pkey_get(pmu, &off) == ESP_OK) {
        printf("PWR hold  : %d s = power off\n", 4 + 2 * (int)off);
    }
    uint8_t on_src = 0;
    uint8_t off_src = 0;
    if (axp2101_power_sources(pmu, &on_src, &off_src) == ESP_OK) {
        printf("sources   : power-on 0x%02X, last power-off 0x%02X\n", on_src, off_src);
    }
    printf("rails     :");
    for (int r = 0; r < AXP2101_RAIL_COUNT; r++) {
        bool on = false;
        uint16_t mv = 0;
        if (axp2101_rail_get(pmu, (axp2101_rail_t)r, &on, &mv) == ESP_OK) {
            printf("%s %s %s %umV", r % 5 == 0 ? (r ? "\n           " : " ") : ",", axp2101_rail_info(r)->name,
                   on ? "ON" : "off", mv);
        }
    }
    printf("\n");
    return 0;
}

static int pmu_rail(axp2101_handle_t pmu, int argc, char **argv)
{
    if (argc < 4) {
        printf("usage: pmu rail <DCDC1..DLDO2> on|off [mV]\n");
        return 1;
    }
    for (int r = 0; r < AXP2101_RAIL_COUNT; r++) {
        if (strcasecmp(argv[2], axp2101_rail_info(r)->name) == 0) {
            const bool on = strcmp(argv[3], "on") == 0;
            const uint16_t mv = argc >= 5 ? (uint16_t)atoi(argv[4]) : 0;
            const esp_err_t err = axp2101_rail_set(pmu, (axp2101_rail_t)r, on, mv);
            printf("%s -> %s\n", axp2101_rail_info(r)->name, esp_err_to_name(err));
            return err == ESP_OK ? 0 : 1;
        }
    }
    printf("unknown rail %s\n", argv[2]);
    return 1;
}

static int cmd_pmu(int argc, char **argv)
{
    axp2101_handle_t pmu = bsp_pmu_handle();
    if (!pmu) {
        printf("PMU not initialised\n");
        return 1;
    }
    if (argc == 1) {
        return pmu_print(pmu);
    }
    if (strcmp(argv[1], "rail") == 0) {
        return pmu_rail(pmu, argc, argv);
    }
    if (strcmp(argv[1], "off") == 0) {
        printf("powering off (press PWR to turn on)\n");
        fflush(stdout);
        return axp2101_shutdown(pmu) == ESP_OK ? 0 : 1;
    }
    printf("usage: pmu | pmu rail <name> on|off [mV] | pmu off\n");
    return 1;
}

static void on_pmu_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    ESP_LOGI(TAG, "PMU event: %s", bsp_pmu_event_name((bsp_pmu_event_t)id));
}

esp_err_t diag_register_pmu(void)
{
    ESP_RETURN_ON_ERROR(esp_event_handler_register(BSP_PMU_EVENT, ESP_EVENT_ANY_ID, on_pmu_event, NULL), TAG,
                        "events");
    const esp_console_cmd_t cmd = {
        .command = "pmu",
        .help = "AXP2101: no args = print battery/VBUS/charger/temp/rails; rail <name> on|off [mV]; off = power down",
        .hint = "[rail|off]",
        .func = cmd_pmu,
    };
    return esp_console_cmd_register(&cmd);
}
