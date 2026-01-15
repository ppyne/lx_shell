#include "sdcard.h"

#include "esp_log.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define PIN_NUM_MISO GPIO_NUM_39
#define PIN_NUM_MOSI GPIO_NUM_14
#define PIN_NUM_CLK  GPIO_NUM_40
#define PIN_NUM_CS   GPIO_NUM_12

static const char* TAG = "SDCARD";
static const char* MOUNT_POINT = "/sdcard";

static bool mounted = false;
static sdmmc_card_t* card = nullptr;

bool sd_mount(bool format_if_failed)
{
    if (mounted) {
        return true;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 400;   // OBLIGATOIRE pour init SD

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = PIN_NUM_MOSI;
    bus_cfg.miso_io_num = PIN_NUM_MISO;
    bus_cfg.sclk_io_num = PIN_NUM_CLK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4000;

    esp_err_t ret = spi_bus_initialize(
        (spi_host_device_t)host.slot,
        &bus_cfg,
        SDSPI_DEFAULT_DMA
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        return false;
    }

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = PIN_NUM_CS;
    slot_cfg.host_id = (spi_host_device_t)host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = format_if_failed,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ret = esp_vfs_fat_sdspi_mount(
        MOUNT_POINT,
        &host,
        &slot_cfg,
        &mount_cfg,
        &card
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sd mount failed: %s", esp_err_to_name(ret));
        spi_bus_free((spi_host_device_t)host.slot);
        card = nullptr;
        return false;
    }

    sdmmc_card_print_info(stdout, card);
    mounted = true;
    return true;
}

void sd_umount()
{
    if (!mounted) {
        return;
    }

    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_free((spi_host_device_t)host.slot);

    card = nullptr;
    mounted = false;
}

bool sd_is_mounted()
{
    return mounted;
}
