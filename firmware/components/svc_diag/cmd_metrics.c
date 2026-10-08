// Console: `metrics [clear]` — persistent metrics ring (boot count, reset reasons,
// heap minimum, drain, BLE disconnects), P2-08.
#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "svc_diag.h"
#include "svc_diag_priv.h"
#include "svc_power.h"

static void print_bytes(const char *label, uint32_t v)
{
    if (v == UINT32_MAX) {
        printf("%s: n/a\n", label);
    } else {
        printf("%s: %lu B\n", label, (unsigned long)v);
    }
}

static void print_entry(const metric_entry_t *e)
{
    printf("  #%-4lu %-8s ", (unsigned long)e->seq, metrics_kind_name(e->kind));
    switch (e->kind) {
    case METRIC_BOOT:
        printf("reset %s", metrics_reset_name(e->a));
        if (e->b != UINT32_MAX) {
            printf(", previous boot min free heap %lu B", (unsigned long)e->b);
        }
        break;
    case METRIC_DRAIN:
        printf("state %s, %lu.%lu mA", power_state_name((power_state_t)e->a), (unsigned long)(e->b / 10),
               (unsigned long)(e->b % 10));
        break;
    case METRIC_BLE_DISCONNECT:
        printf("reason 0x%02lx after %lu s", (unsigned long)e->a, (unsigned long)e->b);
        break;
    default:
        printf("a=%lu b=%lu", (unsigned long)e->a, (unsigned long)e->b);
        break;
    }
    printf("\n");
}

static int cmd_metrics(int argc, char **argv)
{
    if (argc > 1) {
        if (strcmp(argv[1], "clear") != 0) {
            printf("usage: metrics [clear]\n");
            return 1;
        }
        svc_diag_metrics_clear();
        printf("metrics cleared\n");
        return 0;
    }
    static metrics_t m; // console task only
    svc_diag_metrics_get(&m);
    printf("boots: %lu\nreset reasons:", (unsigned long)m.boot_count);
    for (unsigned i = 0; i < METRICS_RESET_KINDS; i++) {
        if (m.reset_counts[i]) {
            printf(" %s %lu", metrics_reset_name(i), (unsigned long)m.reset_counts[i]);
        }
    }
    printf("\n");
    print_bytes("min free heap, all boots", m.min_free_heap);
    print_bytes("min free internal, all boots", m.min_free_internal);
    print_bytes("min free heap, this boot", m.session_min_free);
    printf("ring: %u of %u entries (oldest first)\n", m.count, METRICS_RING_LEN);
    for (size_t i = 0; i < m.count; i++) {
        print_entry(metrics_at(&m, i));
    }
    return 0;
}

esp_err_t diag_register_metrics(void)
{
    const esp_console_cmd_t cmd = {
        .command = "metrics",
        .help = "Persistent metrics: boots, reset reasons, min heap, drain, BLE disconnects; 'clear' empties them",
        .func = cmd_metrics,
    };
    return esp_console_cmd_register(&cmd);
}
