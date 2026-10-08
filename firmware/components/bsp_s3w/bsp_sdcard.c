// TF card on SPI3 (SDSPI, 1-bit SPI wiring per schematic) with FAT at a caller-given
// mount point. No card-detect pin: mount failures are normal (no card) and callers
// detect removal through I/O errors / bsp_sdcard_present().
#include "bsp_s3w.h"

#include <string.h>

#include "bsp_s3w_pins.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "bsp_sdcard";

#define SD_SPI_HOST      SPI3_HOST // SPI2 drives the AMOLED
#define SD_MAX_FILES     8
#define SD_ALLOC_UNIT    (16 * 1024)

static bool s_bus_ready;
static sdmmc_card_t *s_card;
static char s_base[16];

static esp_err_t bus_init(void)
{
    if (s_bus_ready) {
        return ESP_OK;
    }
    const spi_bus_config_t bus = {
        .mosi_io_num = BSP_PIN_SD_MOSI,
        .miso_io_num = BSP_PIN_SD_MISO,
        .sclk_io_num = BSP_PIN_SD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SD_SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");
    s_bus_ready = true;
    return ESP_OK;
}

esp_err_t bsp_sdcard_mount(const char *base_path, sdmmc_card_t **out_card)
{
    ESP_RETURN_ON_FALSE(base_path && strlen(base_path) < sizeof s_base, ESP_ERR_INVALID_ARG, TAG, "path");
    ESP_RETURN_ON_FALSE(!s_card, ESP_ERR_INVALID_STATE, TAG, "already mounted");
    ESP_RETURN_ON_ERROR(bus_init(), TAG, "bus");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = BSP_PIN_SD_CS;
    slot.host_id = SD_SPI_HOST;
    const esp_vfs_fat_mount_config_t mount = {
        .format_if_mount_failed = false, // never wipe a user's card
        .max_files = SD_MAX_FILES,
        .allocation_unit_size = SD_ALLOC_UNIT,
    };
    esp_err_t err = esp_vfs_fat_sdspi_mount(base_path, &host, &slot, &mount, &s_card);
    if (err != ESP_OK) {
        s_card = NULL;
        return err;
    }
    strcpy(s_base, base_path);
    if (out_card) {
        *out_card = s_card;
    }
    return ESP_OK;
}

esp_err_t bsp_sdcard_unmount(void)
{
    if (!s_card) {
        return ESP_OK;
    }
    esp_err_t err = esp_vfs_fat_sdcard_unmount(s_base, s_card);
    s_card = NULL;
    return err;
}

sdmmc_card_t *bsp_sdcard(void)
{
    return s_card;
}

bool bsp_sdcard_present(void)
{
    return s_card && sdmmc_get_status(s_card) == ESP_OK;
}
