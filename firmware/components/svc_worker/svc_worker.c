#include "svc_worker.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "s3w_task.h"

static const char *TAG = "svc_worker";

#define QUEUE_LEN       16
#define SUBMIT_TIMEOUT_MS 100

typedef struct {
    svc_worker_fn_t fn;
    svc_worker_data_fn_t data_fn;
    void *ctx;
    uint8_t len;
    uint8_t data[SVC_WORKER_DATA_MAX];
} job_t;

static QueueHandle_t s_queue;
static TaskHandle_t s_task;

static void worker_task(void *arg)
{
    (void)arg;
    job_t job;
    for (;;) {
        if (xQueueReceive(s_queue, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (job.fn) {
            job.fn(job.ctx);
        } else {
            job.data_fn(job.data, job.len);
        }
    }
}

esp_err_t svc_worker_start(void)
{
    if (s_task) {
        return ESP_OK;
    }
    s_queue = xQueueCreate(QUEUE_LEN, sizeof(job_t));
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "queue");
    // Internal-RAM stack: jobs write flash, which disables the cache.
    const s3w_task_cfg_t cfg = {
        .name = "worker",
        .fn = worker_task,
        .stack_bytes = S3W_STACK_WORKER,
        .prio = S3W_PRIO_WORKER,
        .core = S3W_CORE_SERVICES,
    };
    return s3w_task_create(&cfg, &s_task);
}

static esp_err_t enqueue(const job_t *job)
{
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_INVALID_STATE, TAG, "not started");
    if (xQueueSend(s_queue, job, pdMS_TO_TICKS(SUBMIT_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "queue full");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t svc_worker_submit(svc_worker_fn_t fn, void *ctx)
{
    ESP_RETURN_ON_FALSE(fn, ESP_ERR_INVALID_ARG, TAG, "fn");
    const job_t job = {.fn = fn, .ctx = ctx};
    return enqueue(&job);
}

esp_err_t svc_worker_submit_copy(svc_worker_data_fn_t fn, const void *data, size_t len)
{
    ESP_RETURN_ON_FALSE(fn && len <= SVC_WORKER_DATA_MAX && (data || !len), ESP_ERR_INVALID_ARG, TAG, "args");
    job_t job = {.data_fn = fn, .len = (uint8_t)len};
    if (len) {
        memcpy(job.data, data, len);
    }
    return enqueue(&job);
}

bool svc_worker_is_current(void)
{
    return s_task && xTaskGetCurrentTaskHandle() == s_task;
}
