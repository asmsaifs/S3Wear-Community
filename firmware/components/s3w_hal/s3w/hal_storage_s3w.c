#include "hal_storage.h"

#include "bsp_s3w.h"

esp_err_t hal_sd_mount(const char *base_path)
{
    return bsp_sdcard() ? ESP_OK : bsp_sdcard_mount(base_path, NULL);
}

esp_err_t hal_sd_unmount(void)
{
    return bsp_sdcard_unmount();
}

bool hal_sd_is_mounted(void)
{
    return bsp_sdcard() != NULL;
}

bool hal_sd_present(void)
{
    return bsp_sdcard_present();
}
