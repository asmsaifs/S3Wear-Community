// Console: `activity [minutes | reset]` — svc_activity's day, detector and log (P5-01).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_heap_caps.h"
#include "svc_activity.h"
#include "svc_diag_priv.h"
#include "svc_sensors.h"

static int activity_status(void)
{
    svc_activity_state_t a;
    svc_activity_get(&a);
    const activity_summary_t *t = &a.today;
    printf("day %lu: %lu steps (goal %lu), %lu m, %lu kcal (%lu active), %u active min (goal %u)\n",
           (unsigned long)t->day, (unsigned long)t->steps, (unsigned long)t->step_goal, (unsigned long)t->distance_m,
           (unsigned long)t->kcal, (unsigned long)t->active_kcal, t->active_min, t->active_goal_min);
    printf("walking %s, cadence %u spm\n", a.walking ? "yes" : "no", a.cadence_spm);
    svc_activity_stats_t st;
    svc_activity_get_stats(&st);
    svc_sensors_stats_t ss;
    svc_sensors_get_stats(&ss);
    printf("batches %lu, samples %lu, gaps %lu, detector steps %lu since boot; IMU mode %s, FIFO %lu us/sample\n",
           (unsigned long)st.batches, (unsigned long)st.samples, (unsigned long)st.restarts,
           (unsigned long)st.detector_steps, svc_sensors_mode_name(ss.mode), (unsigned long)ss.fifo_period_us);
    printf("saves %lu (errors %lu)\n", (unsigned long)st.saves, (unsigned long)st.save_errors);
    if (st.logging || st.log_samples) {
        printf("log %s: %s, %lu samples, %lu dropped\n", st.logging ? "running" : "done", st.log_path,
               (unsigned long)st.log_samples, (unsigned long)st.log_dropped);
    }
    return 0;
}

static int activity_minutes(void)
{
    uint16_t *m = heap_caps_malloc(ACT_MINUTES * sizeof *m, MALLOC_CAP_SPIRAM);
    if (!m) {
        return 1;
    }
    svc_activity_get_minutes(m, ACT_MINUTES);
    for (int i = 0; i < ACT_MINUTES; i++) {
        if (m[i]) {
            printf("%02d:%02d %u\n", i / 60, i % 60, m[i]);
        }
    }
    free(m);
    return 0;
}

static int cmd_activity(int argc, char **argv)
{
    if (argc < 2) {
        return activity_status();
    }
    if (strcmp(argv[1], "minutes") == 0) {
        return activity_minutes();
    }
    if (strcmp(argv[1], "reset") == 0) {
        return svc_activity_reset_today() == ESP_OK ? 0 : 1;
    }
    printf("usage: activity | activity minutes | activity reset\n");
    return 1;
}

esp_err_t diag_register_activity(void)
{
    const esp_console_cmd_t cmd = {
        .command = "activity",
        .help = "svc_activity: today's steps, distance, kcal, active minutes, detector and log counters; "
                "minutes = steps per minute today; reset = start today from zero",
        .hint = "minutes|reset",
        .func = cmd_activity,
    };
    return esp_console_cmd_register(&cmd);
}
