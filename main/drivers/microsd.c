#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "microsd.h"

static const char *TAG = "MICROSD";

static sdmmc_card_t *card = NULL;

esp_err_t init_microsd(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    spi_bus_config_t bus_config = {
        .mosi_io_num = MICROSD_MOSI_PIN,
        .miso_io_num = MICROSD_MISO_PIN,
        .sclk_io_num = MICROSD_CLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000
    };
    esp_err_t err = spi_bus_initialize(host.slot, &bus_config, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(err));
        return err;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = MICROSD_CS_PIN;
    slot_config.host_id = host.slot;

    err = esp_vfs_fat_sdspi_mount(MICROSD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. If you want the card to be formatted, set format_if_mount_failed = true.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). Card inserted? FAT formatted? wiring is correct?.", esp_err_to_name(err));
        }
        return err;
    }
    ESP_LOGI(TAG, "Filesystem mounted at %s", MICROSD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, card); // logs name, type, capacity, etc.

    return ESP_OK;
}


