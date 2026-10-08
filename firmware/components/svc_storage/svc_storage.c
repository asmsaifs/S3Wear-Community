#include "svc_storage.h"

#include <string.h>

#include "hal_storage.h"
#include "esp_check.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "s3w_event.h"
#include "svc_storage_events.h"
#include "svc_worker.h"

static const char *TAG = "svc_storage";

ESP_EVENT_DEFINE_BASE(SVC_STORAGE_EVENT);

#define FLASH_PARTITION  "storage"

static SemaphoreHandle_t s_sd_lock;
static bool s_flash_mounted;

static esp_err_t sd_lock_init(void)
{
    if (!s_sd_lock) {
        s_sd_lock = xSemaphoreCreateMutex();
    }
    return s_sd_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t svc_storage_mount_flash(void)
{
    if (s_flash_mounted) {
        return ESP_OK;
    }
    const esp_vfs_littlefs_conf_t conf = {
        .base_path = SVC_STORAGE_FLASH_PATH,
        .partition_label = FLASH_PARTITION,
        .format_if_mount_failed = true,
    };
    ESP_RETURN_ON_ERROR(esp_vfs_littlefs_register(&conf), TAG, "littlefs");
    s_flash_mounted = true;
    size_t total = 0;
    size_t used = 0;
    esp_littlefs_info(FLASH_PARTITION, &total, &used);
    ESP_LOGI(TAG, "%s: LittleFS %u KB used of %u KB", SVC_STORAGE_FLASH_PATH, (unsigned)(used / 1024),
             (unsigned)(total / 1024));
    return ESP_OK;
}

esp_err_t svc_storage_sd_mount(void)
{
    ESP_RETURN_ON_ERROR(sd_lock_init(), TAG, "lock");
    xSemaphoreTake(s_sd_lock, portMAX_DELAY);
    esp_err_t err = hal_sd_mount(SVC_STORAGE_SD_PATH);
    xSemaphoreGive(s_sd_lock);
    if (err == ESP_OK) {
        s3w_event_post(SVC_STORAGE_EVENT, SVC_STORAGE_EVT_SD_MOUNTED, NULL, 0);
    }
    return err;
}

esp_err_t svc_storage_sd_unmount(void)
{
    ESP_RETURN_ON_ERROR(sd_lock_init(), TAG, "lock");
    xSemaphoreTake(s_sd_lock, portMAX_DELAY);
    const bool was = hal_sd_is_mounted();
    esp_err_t err = hal_sd_unmount();
    xSemaphoreGive(s_sd_lock);
    if (was) {
        s3w_event_post(SVC_STORAGE_EVENT, SVC_STORAGE_EVT_SD_UNMOUNTED, NULL, 0);
    }
    return err;
}

bool svc_storage_sd_check(void)
{
    if (sd_lock_init() != ESP_OK) {
        return false;
    }
    xSemaphoreTake(s_sd_lock, portMAX_DELAY);
    bool ok = hal_sd_present();
    bool removed = false;
    if (!ok && hal_sd_is_mounted()) {
        hal_sd_unmount();
        removed = true;
    }
    xSemaphoreGive(s_sd_lock);
    if (removed) {
        ESP_LOGW(TAG, "SD card stopped answering: unmounted");
        s3w_event_post(SVC_STORAGE_EVENT, SVC_STORAGE_EVT_SD_REMOVED, NULL, 0);
    }
    return ok;
}

esp_err_t svc_storage_usage(const char *mount_path, uint64_t *total, uint64_t *used)
{
    if (strcmp(mount_path, SVC_STORAGE_FLASH_PATH) == 0) {
        size_t t = 0;
        size_t u = 0;
        ESP_RETURN_ON_ERROR(esp_littlefs_info(FLASH_PARTITION, &t, &u), TAG, "littlefs info");
        *total = t;
        *used = u;
        return ESP_OK;
    }
    if (strcmp(mount_path, SVC_STORAGE_SD_PATH) == 0) {
        uint64_t t = 0;
        uint64_t f = 0;
        if (!hal_sd_is_mounted()) {
            return ESP_ERR_INVALID_STATE; // no card is normal, not an error
        }
        ESP_RETURN_ON_ERROR(esp_vfs_fat_info(SVC_STORAGE_SD_PATH, &t, &f), TAG, "fat info");
        *total = t;
        *used = t - f;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

// Boot step 6: mounting a missing card takes hundreds of ms of SPI timeouts, so it
// runs on the worker, off the boot path.
static void sd_mount_job(void *arg)
{
    (void)arg;
    const esp_err_t err = svc_storage_sd_mount();
    if (err == ESP_OK) {
        uint64_t total = 0;
        uint64_t used = 0;
        svc_storage_usage(SVC_STORAGE_SD_PATH, &total, &used);
        ESP_LOGI(TAG, "%s: FAT %llu MB used of %llu MB", SVC_STORAGE_SD_PATH, used >> 20, total >> 20);
    } else {
        ESP_LOGI(TAG, "no SD card (%s)", esp_err_to_name(err));
    }
}

esp_err_t svc_storage_start(void)
{
    ESP_RETURN_ON_ERROR(sd_lock_init(), TAG, "lock");
    return svc_worker_submit(sd_mount_job, NULL);
}
