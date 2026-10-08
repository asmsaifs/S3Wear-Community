// Console: `heap`, `tasks`, `log` — runtime state of the system (P2-08).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "svc_diag_priv.h"

static void heap_line(const char *name, uint32_t caps)
{
    multi_heap_info_t i;
    heap_caps_get_info(&i, caps);
    printf("%-9s total %8lu  free %8lu  min free %8lu  largest block %8lu\n", name,
           (unsigned long)(i.total_free_bytes + i.total_allocated_bytes), (unsigned long)i.total_free_bytes,
           (unsigned long)i.minimum_free_bytes, (unsigned long)i.largest_free_block);
}

static int cmd_heap(int argc, char **argv)
{
    heap_line("internal", MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    heap_line("DMA", MALLOC_CAP_DMA);
    heap_line("PSRAM", MALLOC_CAP_SPIRAM);
    if (argc > 1 && strcmp(argv[1], "check") == 0) {
        printf("heap integrity: %s\n", heap_caps_check_integrity_all(true) ? "ok" : "CORRUPT");
    }
    return 0;
}

static const char *state_name(eTaskState s)
{
    static const char *const names[] = {"run", "ready", "block", "susp", "del", "inval"};
    return (unsigned)s < sizeof(names) / sizeof(names[0]) ? names[s] : "?";
}

static int cmd_tasks(int argc, char **argv)
{
    (void)argc;
    (void)argv;
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
    const UBaseType_t n = uxTaskGetNumberOfTasks();
    TaskStatus_t *st = calloc(n, sizeof(*st));
    if (!st) {
        printf("out of memory\n");
        return 1;
    }
    const UBaseType_t got = uxTaskGetSystemState(st, n, NULL);
    printf("%-16s %-5s %4s %4s %10s\n", "task", "state", "prio", "core", "stack free");
    for (UBaseType_t i = 0; i < got; i++) {
        char core[4] = "-";
#if CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
        if (st[i].xCoreID == 0 || st[i].xCoreID == 1) {
            snprintf(core, sizeof(core), "%d", (int)st[i].xCoreID);
        }
#endif
        // High-water mark is in StackType_t words (bytes on Xtensa ESP-IDF).
        printf("%-16s %-5s %4u %4s %8lu B\n", st[i].pcTaskName, state_name(st[i].eCurrentState),
               (unsigned)st[i].uxCurrentPriority, core, (unsigned long)st[i].usStackHighWaterMark);
    }
    printf("%u tasks\n", (unsigned)got);
    free(st);
    return 0;
#else
    printf("tasks: needs CONFIG_FREERTOS_USE_TRACE_FACILITY\n");
    return 1;
#endif
}

static int cmd_log(int argc, char **argv)
{
    static const char *const names[] = {"none", "error", "warn", "info", "debug", "verbose"};
    if (argc != 3) {
        printf("usage: log <tag|*> <none|error|warn|info|debug|verbose>\n");
        return 1;
    }
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strcmp(argv[2], names[i]) == 0) {
            esp_log_level_set(argv[1], (esp_log_level_t)i);
            printf("%s -> %s (a level above the compile-time maximum has no effect)\n", argv[1], names[i]);
            return 0;
        }
    }
    printf("unknown level '%s'\n", argv[2]);
    return 1;
}

esp_err_t diag_register_sys(void)
{
    const esp_console_cmd_t cmds[] = {
        {.command = "heap", .help = "Heap: internal, DMA and PSRAM free/min/largest; 'heap check' walks the heap",
         .func = cmd_heap},
        {.command = "tasks", .help = "FreeRTOS tasks: state, priority, core, stack never used", .func = cmd_tasks},
        {.command = "log", .help = "log <tag|*> <level>: set the runtime log level", .func = cmd_log},
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t err = esp_console_cmd_register(&cmds[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
