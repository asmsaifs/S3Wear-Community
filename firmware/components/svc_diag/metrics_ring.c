#include "metrics_ring.h"

#include <string.h>

#define METRICS_MAGIC 0x3D57C0DEu

static uint32_t crc32_calc(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t crc = 0xFFFFFFFFu;
    while (len--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static uint32_t metrics_crc(const metrics_t *m)
{
    return crc32_calc(m, offsetof(metrics_t, crc));
}

void metrics_init(metrics_t *m)
{
    memset(m, 0, sizeof(*m));
    m->magic = METRICS_MAGIC;
    m->version = METRICS_VERSION;
    m->next_seq = 1;
    m->min_free_heap = UINT32_MAX;
    m->min_free_internal = UINT32_MAX;
    m->session_min_free = UINT32_MAX;
}

void metrics_push(metrics_t *m, metric_kind_t kind, uint32_t a, uint32_t b)
{
    if ((unsigned)kind >= METRIC_KIND_COUNT) {
        return;
    }
    metric_entry_t *e = &m->entries[m->head];
    memset(e, 0, sizeof(*e));
    e->seq = m->next_seq++;
    e->kind = (uint8_t)kind;
    e->a = a;
    e->b = b;
    m->head = (uint16_t)((m->head + 1) % METRICS_RING_LEN);
    if (m->count < METRICS_RING_LEN) {
        m->count++;
    }
}

const metric_entry_t *metrics_at(const metrics_t *m, size_t i)
{
    if (i >= m->count) {
        return NULL;
    }
    const size_t oldest = (m->head + METRICS_RING_LEN - m->count) % METRICS_RING_LEN;
    return &m->entries[(oldest + i) % METRICS_RING_LEN];
}

void metrics_note_boot(metrics_t *m, unsigned reset_reason)
{
    m->boot_count++;
    if (reset_reason >= METRICS_RESET_KINDS) {
        reset_reason = 0;
    }
    m->reset_counts[reset_reason]++;
    metrics_push(m, METRIC_BOOT, reset_reason, m->session_min_free);
    m->session_min_free = UINT32_MAX;
}

void metrics_note_heap(metrics_t *m, uint32_t min_free, uint32_t min_free_internal)
{
    if (min_free < m->session_min_free) {
        m->session_min_free = min_free;
    }
    if (min_free < m->min_free_heap) {
        m->min_free_heap = min_free;
    }
    if (min_free_internal < m->min_free_internal) {
        m->min_free_internal = min_free_internal;
    }
}

void metrics_seal(metrics_t *m)
{
    m->crc = metrics_crc(m);
}

bool metrics_load(metrics_t *m, const void *blob, size_t len)
{
    metrics_t tmp;
    if (blob && len == sizeof(tmp)) {
        memcpy(&tmp, blob, sizeof(tmp));
        if (tmp.magic == METRICS_MAGIC && tmp.version == METRICS_VERSION && tmp.count <= METRICS_RING_LEN &&
            tmp.head < METRICS_RING_LEN && tmp.crc == metrics_crc(&tmp)) {
            *m = tmp;
            return true;
        }
    }
    metrics_init(m);
    return false;
}

const char *metrics_reset_name(unsigned reason)
{
    static const char *const names[METRICS_RESET_KINDS] = {
        "unknown", "power-on", "external", "software", "panic",    "int-wdt",   "task-wdt",   "wdt",
        "deep-sleep", "brownout", "sdio",   "usb",      "jtag",     "efuse",     "pwr-glitch", "cpu-lockup",
    };
    return reason < METRICS_RESET_KINDS ? names[reason] : "?";
}

const char *metrics_kind_name(unsigned kind)
{
    static const char *const names[METRIC_KIND_COUNT] = {"boot", "drain", "ble-disc"};
    return kind < METRIC_KIND_COUNT ? names[kind] : "?";
}
