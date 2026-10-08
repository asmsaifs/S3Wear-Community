#include "s3w_task.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"

static const char *TAG = "s3w_task";

esp_err_t s3w_task_create(const s3w_task_cfg_t *cfg, TaskHandle_t *out)
{
    ESP_RETURN_ON_FALSE(cfg && cfg->fn && cfg->name, ESP_ERR_INVALID_ARG, TAG, "cfg");
    TaskHandle_t h = NULL;
    BaseType_t ok;
    if (cfg->stack_psram) {
        ok = xTaskCreatePinnedToCoreWithCaps(cfg->fn, cfg->name, cfg->stack_bytes, cfg->arg, cfg->prio, &h, cfg->core,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    } else {
        ok = xTaskCreatePinnedToCore(cfg->fn, cfg->name, cfg->stack_bytes, cfg->arg, cfg->prio, &h, cfg->core);
    }
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task %s (%lu B stack)", cfg->name,
                        (unsigned long)cfg->stack_bytes);
    if (out) {
        *out = h;
    }
    return ESP_OK;
}

s3w_mutex_t s3w_mutex_create(void)
{
    return xSemaphoreCreateRecursiveMutex();
}

bool s3w_mutex_lock(s3w_mutex_t m, uint32_t timeout_ms)
{
    const TickType_t t = timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(m, t) == pdTRUE;
}

void s3w_mutex_unlock(s3w_mutex_t m)
{
    xSemaphoreGiveRecursive(m);
}

void s3w_mutex_delete(s3w_mutex_t m)
{
    if (m) {
        vSemaphoreDelete(m);
    }
}
