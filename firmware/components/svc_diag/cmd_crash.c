// Console: `coredump info|dump|erase` and `crash <mode>` — core dump in flash (P2-08).
// The dump is retrieved as base64 between markers; decode it on the host with
//   espcoredump.py info_corefile -t b64 -c dump.b64 build/s3wear.elf
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_core_dump.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "svc_diag_priv.h"

// Raw bytes per output line; a multiple of 3 so lines concatenate into one valid base64 stream.
#define DUMP_CHUNK 192

static int coredump_info(void)
{
    size_t addr, size;
    esp_err_t err = esp_core_dump_image_get(&addr, &size);
    if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_INVALID_SIZE) {
        printf("no core dump in flash\n");
        return 0;
    }
    if (err != ESP_OK) {
        printf("core dump: %s\n", esp_err_to_name(err));
        return 1;
    }
    err = esp_core_dump_image_check();
    printf("core dump: %lu bytes at flash 0x%lx, image %s\n", (unsigned long)size, (unsigned long)addr,
           err == ESP_OK ? "valid" : esp_err_to_name(err));
    static esp_core_dump_summary_t sum; // console task only
    if (err == ESP_OK && esp_core_dump_get_summary(&sum) == ESP_OK) {
        printf("crashed task %s, PC 0x%08lx, exception cause %lu\nbacktrace:", sum.exc_task, (unsigned long)sum.exc_pc,
               (unsigned long)sum.ex_info.exc_cause);
        for (uint32_t i = 0; i < sum.exc_bt_info.depth && i < 16; i++) {
            printf(" 0x%08lx", (unsigned long)sum.exc_bt_info.bt[i]);
        }
        printf("%s\n", sum.exc_bt_info.corrupted ? " (corrupted)" : "");
    }
    return 0;
}

static int coredump_dump(void)
{
    size_t addr, size;
    if (esp_core_dump_image_check() != ESP_OK || esp_core_dump_image_get(&addr, &size) != ESP_OK) {
        printf("no valid core dump in flash\n");
        return 1;
    }
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    if (!part || addr < part->address || addr + size > part->address + part->size) {
        printf("core dump partition not found\n");
        return 1;
    }
    uint8_t *raw = malloc(DUMP_CHUNK);
    unsigned char *b64 = malloc(DUMP_CHUNK / 3 * 4 + 4);
    if (!raw || !b64) {
        free(raw);
        free(b64);
        printf("out of memory\n");
        return 1;
    }
    printf("================= CORE DUMP START =================\n");
    for (size_t off = 0; off < size; off += DUMP_CHUNK) {
        const size_t n = size - off < DUMP_CHUNK ? size - off : DUMP_CHUNK;
        size_t olen = 0;
        if (esp_partition_read(part, addr - part->address + off, raw, n) != ESP_OK ||
            mbedtls_base64_encode(b64, DUMP_CHUNK / 3 * 4 + 4, &olen, raw, n) != 0) {
            printf("\nread error at offset %lu\n", (unsigned long)off);
            break;
        }
        printf("%.*s\n", (int)olen, b64);
    }
    printf("================= CORE DUMP END =================\n");
    free(raw);
    free(b64);
    return 0;
}

static int cmd_coredump(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "info";
    if (strcmp(sub, "info") == 0) {
        return coredump_info();
    }
    if (strcmp(sub, "dump") == 0) {
        return coredump_dump();
    }
    if (strcmp(sub, "erase") == 0) {
        esp_err_t err = esp_core_dump_image_erase();
        printf("%s\n", err == ESP_OK ? "core dump erased" : esp_err_to_name(err));
        return err == ESP_OK ? 0 : 1;
    }
    printf("usage: coredump [info|dump|erase]\n");
    return 1;
}

static int cmd_crash(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "";
    if (strcmp(mode, "null") == 0) {
        printf("crashing: store to NULL\n");
        fflush(stdout);
        *(volatile int *)NULL = 1;
    } else if (strcmp(mode, "abort") == 0) {
        printf("crashing: abort()\n");
        fflush(stdout);
        abort();
    } else if (strcmp(mode, "intwdt") == 0) {
        printf("crashing: interrupts off, interrupt watchdog\n");
        fflush(stdout);
        portDISABLE_INTERRUPTS();
        for (;;) {
        }
    }
    printf("usage: crash <null|abort|intwdt>  (reboots the watch; 'coredump info' afterwards)\n");
    return 1;
}

esp_err_t diag_register_crash(void)
{
    const esp_console_cmd_t cmds[] = {
        {.command = "coredump", .help = "Core dump in flash: info (default), dump (base64 for espcoredump.py), erase",
         .func = cmd_coredump},
        {.command = "crash", .help = "Force a panic to test the core dump: null | abort | intwdt", .func = cmd_crash},
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t err = esp_console_cmd_register(&cmds[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
