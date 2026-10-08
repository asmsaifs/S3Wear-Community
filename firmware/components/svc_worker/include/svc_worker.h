// Worker service: one low-priority task (core 0) that runs slow jobs in order —
// flash writes, file operations, SD mount — so services don't spawn ad-hoc tasks
// and the UI task never blocks (docs/02 §3).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SVC_WORKER_DATA_MAX 64

typedef void (*svc_worker_fn_t)(void *ctx);
typedef void (*svc_worker_data_fn_t)(const void *data, size_t len);

esp_err_t svc_worker_start(void);

/** Queue fn(ctx). ESP_ERR_TIMEOUT if the queue stays full for 100 ms. Not from ISR. */
esp_err_t svc_worker_submit(svc_worker_fn_t fn, void *ctx);

/** Queue fn(copy of data). len <= SVC_WORKER_DATA_MAX. */
esp_err_t svc_worker_submit_copy(svc_worker_data_fn_t fn, const void *data, size_t len);

/** True when called from the worker task (e.g. to assert a function runs there). */
bool svc_worker_is_current(void);

#ifdef __cplusplus
}
#endif
