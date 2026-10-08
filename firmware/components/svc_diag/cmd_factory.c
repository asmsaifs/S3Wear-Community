// Console: `factory test [auto]` — run the factory test (P1-09). The result is one JSON
// line on stdout; `auto` skips the steps that need a person (buttons, tap).
#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "factory_test.h"
#include "svc_diag_priv.h"

static int cmd_factory(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "test") == 0) {
        const bool interactive = !(argc >= 3 && strcmp(argv[2], "auto") == 0);
        return factory_test_run(interactive) == ESP_OK ? 0 : 1;
    }
    printf("usage: factory test [auto]\n");
    return 1;
}

esp_err_t diag_register_factory(void)
{
    const esp_console_cmd_t cmd = {
        .command = "factory",
        .help = "factory test [auto]: test every chip + BLE + Wi-Fi, checklist on screen, JSON result line",
        .hint = "test [auto]",
        .func = cmd_factory,
    };
    return esp_console_cmd_register(&cmd);
}
