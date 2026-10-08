// Persistent metrics ring (docs/02-firmware-architecture.md §11): boot count, reset
// reasons, battery drain per state, BLE disconnect reasons, minimum free heap.
// Pure C (no ESP-IDF): the struct is the NVS blob, built by firmware/host_test too.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define METRICS_RING_LEN    32
#define METRICS_RESET_KINDS 16 // esp_reset_reason_t values 0..15
#define METRICS_VERSION     1

typedef enum {
    METRIC_BOOT = 0,       // a = reset reason, b = session minimum free heap of the previous boot (bytes)
    METRIC_DRAIN,          // a = power state, b = drain in mA x10
    METRIC_BLE_DISCONNECT, // a = HCI/NimBLE reason code, b = connection time in seconds
    METRIC_KIND_COUNT,
} metric_kind_t;

typedef struct {
    uint32_t seq; // 1-based, never reused
    uint8_t kind; // metric_kind_t
    uint8_t pad[3];
    uint32_t a;
    uint32_t b;
} metric_entry_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count; // valid entries, <= METRICS_RING_LEN
    uint16_t head;  // index of the next write
    uint16_t pad;
    uint32_t next_seq;
    uint32_t boot_count;
    uint32_t reset_counts[METRICS_RESET_KINDS];
    uint32_t min_free_heap;     // all boots, bytes (UINT32_MAX: unknown)
    uint32_t min_free_internal; // all boots, internal RAM
    uint32_t session_min_free;  // this boot so far, reset by metrics_note_boot
    metric_entry_t entries[METRICS_RING_LEN];
    uint32_t crc; // CRC-32 of every byte above, set by metrics_seal
} metrics_t;

void metrics_init(metrics_t *m);

/** Appends an entry, overwriting the oldest when full. kind >= METRIC_KIND_COUNT is ignored. */
void metrics_push(metrics_t *m, metric_kind_t kind, uint32_t a, uint32_t b);

/** Entry i, 0 = oldest. NULL when i >= count. */
const metric_entry_t *metrics_at(const metrics_t *m, size_t i);

/** Boot: counts it and the reset reason, pushes METRIC_BOOT, restarts session_min_free. */
void metrics_note_boot(metrics_t *m, unsigned reset_reason);

/** Folds in the minimum free heap seen so far (whole heap and internal RAM). */
void metrics_note_heap(metrics_t *m, uint32_t min_free, uint32_t min_free_internal);

/** Sets crc; call before writing the blob. */
void metrics_seal(metrics_t *m);

/** Copies a blob read from NVS. false (m reset to empty) on wrong size, magic, version, crc or ring indices. */
bool metrics_load(metrics_t *m, const void *blob, size_t len);

const char *metrics_reset_name(unsigned reason);
const char *metrics_kind_name(unsigned kind);

#ifdef __cplusplus
}
#endif
