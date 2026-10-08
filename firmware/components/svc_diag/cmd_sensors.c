// Console: `sensors [accel [n] | trace on|off]` — svc_sensors raise-to-wake state and
// counters, accelerometer in the watch frame (axis check), per-window log (P3-05).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "raise_detect.h"
#include "svc_diag_priv.h"
#include "svc_power.h"
#include "svc_sensors.h"

static int sensors_status(void)
{
    svc_sensors_stats_t st;
    svc_sensors_get_stats(&st);
    svc_power_stats_t pw;
    svc_power_get_stats(&pw);
    printf("IMU %s, mode %s, raise to wake %s (cone %u deg)\n", st.present ? "found" : "missing",
           svc_sensors_mode_name(st.mode), st.raise_enabled ? "on" : "off", st.cone_deg);
    printf("motion IRQs %lu, windows %lu, raises %lu, samples %lu, errors %lu\n", (unsigned long)st.motion_irqs,
           (unsigned long)st.windows, (unsigned long)st.raises, (unsigned long)st.samples, (unsigned long)st.errors);
    printf("raise wakes %lu, untouched before screen off (likely false) %lu\n",
           (unsigned long)pw.wakes[SVC_POWER_WAKE_RAISE], (unsigned long)pw.raise_unused);
    if (st.windows) {
        printf("last window: %u ms, away %d deg, end %d deg -> %s\n", st.last_ms, st.last_min_deg, st.last_end_deg,
               st.last_raise ? "raise" : "no raise");
    }
    printf("svc_sensors stack: %lu B never used\n", (unsigned long)st.stack_free);
    return 0;
}

static int sensors_accel(int n)
{
    for (int i = 0; i < n; i++) {
        svc_sensors_accel_t a;
        const esp_err_t err = svc_sensors_read_accel(&a);
        if (err != ESP_OK) {
            printf("read failed: %s\n", esp_err_to_name(err));
            return 1;
        }
        printf("x %6ld  y %6ld  z %6ld mg  (%d deg from screen up)\n", (long)a.x, (long)a.y, (long)a.z,
               raise_angle_deg(a.x, a.y, a.z));
        if (i + 1 < n) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    return 0;
}

static int cmd_sensors(int argc, char **argv)
{
    const char *sub = argc >= 2 ? argv[1] : "";
    if (argc < 2) {
        return sensors_status();
    }
    if (strcmp(sub, "accel") == 0) {
        const int n = argc >= 3 ? atoi(argv[2]) : 1;
        return sensors_accel(n > 0 && n <= 120 ? n : 1);
    }
    if (strcmp(sub, "trace") == 0 && argc >= 3 && (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0)) {
        svc_sensors_set_trace(strcmp(argv[2], "on") == 0);
        return 0;
    }
    printf("usage: sensors | sensors accel [n] | sensors trace on|off\n");
    return 1;
}

esp_err_t diag_register_sensors(void)
{
    const esp_console_cmd_t cmd = {
        .command = "sensors",
        .help = "svc_sensors: raise-to-wake state and counters; accel [n] = samples in the watch frame "
                "(face up: z = +1000, 12 o'clock up: y = +1000); trace on|off = log every raise window",
        .func = cmd_sensors,
    };
    return esp_console_cmd_register(&cmd);
}
