// Console: `input` — svc_input press counts, action map and the last touch contact
// (palm-cover tuning, P2-07).
#include <stdio.h>

#include "esp_console.h"
#include "svc_diag_priv.h"
#include "svc_input.h"

static int cmd_input(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    svc_input_stats_t st;
    svc_input_get_stats(&st);
    for (int b = 0; b < SVC_INPUT_BUTTON_COUNT; b++) {
        printf("%-4s", svc_input_button_name((svc_input_button_t)b));
        for (int g = BTN_GESTURE_SHORT; g < BTN_GESTURE_COUNT; g++) {
            printf("  %s %lu -> %s", btn_gesture_name((btn_gesture_t)g), (unsigned long)st.presses[b][g],
                   svc_input_action_name(svc_input_get_action((svc_input_button_t)b, (btn_gesture_t)g)));
        }
        printf("\n");
    }
    printf("wake-only presses %lu, palm covers %lu\n", (unsigned long)st.wake_presses, (unsigned long)st.palms);
    printf("last contact: max %u points, area %u, box %ux%u px%s\n", st.last_points, st.last_area, st.last_w,
           st.last_h, st.last_covered ? " (palm rules matched)" : "");
    return 0;
}

esp_err_t diag_register_input(void)
{
    const esp_console_cmd_t cmd = {
        .command = "input",
        .help = "svc_input: press counts and actions per button, last touch contact (palm tuning)",
        .func = cmd_input,
    };
    return esp_console_cmd_register(&cmd);
}
