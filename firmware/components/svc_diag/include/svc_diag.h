// Diagnostics service: interactive console over USB-Serial-JTAG.
#pragma once

#include "esp_err.h"
#include "metrics_ring.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Register all diagnostic commands and start the REPL task. Call once, after bsp_init_early(). */
esp_err_t svc_diag_console_start(void);

/**
 * Metrics ring (docs/02 §11), persisted in NVS namespace "s3w_diag". Boot step after
 * svc_settings_init (NVS) and svc_worker_start: loads the ring, counts this boot and its
 * reset reason, then saves it every 15 min and on svc_diag_metrics_flush(). Without NVS
 * it runs in RAM only. A boot count and reset reason are recorded even after a panic.
 */
esp_err_t svc_diag_metrics_start(void);

/** Appends a metric from any task (not an ISR). Saved with the next flush. */
void svc_diag_metric_record(metric_kind_t kind, uint32_t a, uint32_t b);

/** Copy of the ring with the heap minimum folded in. */
void svc_diag_metrics_get(metrics_t *out);

/** Queue a save on svc_worker (ESP_ERR_TIMEOUT if its queue stays full). */
esp_err_t svc_diag_metrics_flush(void);

/** Empty the ring and counters (queues a save). */
void svc_diag_metrics_clear(void);

#ifdef __cplusplus
}
#endif
