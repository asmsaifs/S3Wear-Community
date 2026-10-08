// Task and lock wrappers. Every S3Wear task takes its core/priority/stack from the
// table below (docs/02-firmware-architecture.md §3) so the budget lives in one place.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

#define S3W_CORE_SERVICES 0
#define S3W_CORE_UI       1
#define S3W_CORE_ANY      tskNO_AFFINITY

// Priorities (higher runs first). IDF system tasks (Wi-Fi, NimBLE, esp_timer) sit above.
#define S3W_PRIO_POWER   18
#define S3W_PRIO_AUDIO   15
#define S3W_PRIO_VOICE   14
#define S3W_PRIO_ALARM   13 // ringing: feeds the speaker
#define S3W_PRIO_SENSORS 12
#define S3W_PRIO_EVENTS  11
#define S3W_PRIO_LINK    10
#define S3W_PRIO_UI      8
#define S3W_PRIO_APP     7
#define S3W_PRIO_WORKER  5

// Stacks in bytes
#define S3W_STACK_UI     (12 * 1024)
#define S3W_STACK_EVENTS (4 * 1024)
#define S3W_STACK_WORKER (6 * 1024)
#define S3W_STACK_POWER  (4 * 1024)
#define S3W_STACK_SENSORS (3 * 1024) // PSRAM (svc_sensors: no flash writes)
#define S3W_STACK_ALARM  (4 * 1024) // internal (NVS writes)

typedef struct {
    const char *name;
    TaskFunction_t fn;
    void *arg;
    uint32_t stack_bytes;
    UBaseType_t prio;
    BaseType_t core;   // S3W_CORE_*
    bool stack_psram;  // only for tasks that never run with the flash cache off (no flash writes)
} s3w_task_cfg_t;

esp_err_t s3w_task_create(const s3w_task_cfg_t *cfg, TaskHandle_t *out);

// --- Mutex (recursive-safe for nested service calls) -------------------------

typedef SemaphoreHandle_t s3w_mutex_t;

s3w_mutex_t s3w_mutex_create(void);
/** timeout_ms = UINT32_MAX waits forever. */
bool s3w_mutex_lock(s3w_mutex_t m, uint32_t timeout_ms);
void s3w_mutex_unlock(s3w_mutex_t m);
void s3w_mutex_delete(s3w_mutex_t m);

#ifdef __cplusplus
}
#endif
