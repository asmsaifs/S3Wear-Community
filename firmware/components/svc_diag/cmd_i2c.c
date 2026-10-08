// Console: `i2c scan` — probe every 7-bit address on the shared bus.
#include <stdio.h>
#include <string.h>

#include "bsp_s3w.h"
#include "esp_console.h"
#include "svc_diag_priv.h"

#define PROBE_TIMEOUT_MS 20

static int i2c_scan(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_bus();
    if (!bus) {
        printf("I2C bus not initialised\n");
        return 1;
    }
    int found = 0;
    int known = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(bus, addr, PROBE_TIMEOUT_MS) == ESP_OK) {
            const char *name = bsp_i2c_device_name(addr);
            printf("  0x%02X  %s\n", addr, name ? name : "(unknown)");
            found++;
            known += name != NULL;
        }
    }
    printf("%d device(s) found, %d/%d expected on-board devices present\n", found, known, BSP_I2C_DEVICE_COUNT);
    return known == BSP_I2C_DEVICE_COUNT ? 0 : 1;
}

static int cmd_i2c(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "scan") == 0) {
        return i2c_scan();
    }
    printf("usage: i2c scan\n");
    return 1;
}

esp_err_t diag_register_i2c(void)
{
    const esp_console_cmd_t cmd = {
        .command = "i2c",
        .help = "I2C bus tools: 'i2c scan' lists responding addresses",
        .hint = "scan",
        .func = cmd_i2c,
    };
    return esp_console_cmd_register(&cmd);
}
