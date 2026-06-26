#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "microsd.h"

static const char *TAG = "MICROSD";
static const bool TEST = false; // set to true to run a self-test on init

static sdmmc_card_t *card = NULL;

static void build_path(char *out, size_t out_size, const char *path)
{
    // /sdcard is the mount point, and all paths should 
    // start with '/'
    snprintf(out, out_size, "%s%s", MICROSD_MOUNT_POINT, path);
}

esp_err_t microsd_mkdir(const char *path)
{
    char full_path[64];
    build_path(full_path, sizeof(full_path), path);
    ESP_LOGI(TAG, "Creating directory: %s", full_path);
    if (mkdir(full_path, 0775) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "Failed to create directory %s: %s", full_path, strerror(errno));
        return ESP_FAIL;
    }
    return ESP_OK;
}

FILE *microsd_open(const char *path, const char *mode)
{
    char full_path[64];
    build_path(full_path, sizeof(full_path), path);
    FILE *file = fopen(full_path, mode);
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open file %s: %s", full_path, strerror(errno));
    }
    return file;
}

size_t microsd_write(FILE *file, const void *data, size_t len)
{
    return fwrite(data, 1, len, file);
}

esp_err_t microsd_seek(FILE *file, long offset, int whence)
{
    if (fseek(file, offset, whence) != 0) {
        ESP_LOGE(TAG, "Failed to seek in file: %s", strerror(errno));
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t microsd_close(FILE *file)
{
    if (fclose(file) != 0) {
        ESP_LOGE(TAG, "Failed to close file: %s", strerror(errno));
        return ESP_FAIL;
    }
    return ESP_OK;
}

size_t microsd_count_files(const char *path)
{
    char full_path[64];
    build_path(full_path, sizeof(full_path), path);
    DIR *dir = opendir(full_path);
    if (dir == NULL) {
        ESP_LOGW(TAG, "Likely that directory %s does not exist: %s", full_path, strerror(errno));
        return 0;
    }

    size_t count = 0;
    struct dirent *entry;
    while((entry = readdir(dir)) != NULL) {
        // Skip hidden entries: ".", "..", and metadata files like macOS
        // AppleDouble sidecars (._0000.wav) and .DS_Store. Counting these
        // would double the clip total and corrupt clip numbering.
        if(entry->d_name[0] == '.') {
            continue;
        }
        count++;
    }
    closedir(dir);
    return count;
}

size_t microsd_next_index(const char *path)
{
    char full_path[64];
    build_path(full_path, sizeof(full_path), path);
    DIR *dir = opendir(full_path);
    if (dir == NULL) {
        ESP_LOGW(TAG, "Likely that directory %s does not exist: %s", full_path, strerror(errno));
        return 0;
    }
    long max_index = -1;
    struct dirent *entry;
    while((entry = readdir(dir)) != NULL) {
        if(entry->d_name[0] == '.') {
            continue; // skip hidden entries
        }
        char *end = NULL;
        long index = strtol(entry->d_name, &end, 10); // parse leading integer of "NNNN.wav"
        if(end != entry->d_name && index > max_index) {
            max_index = index;
        }
    }
    closedir(dir);
    return (size_t)(max_index + 1); // next index is one more than the max found
}

static esp_err_t microsd_selftest(void)
{
    esp_err_t err = microsd_mkdir("/selftest");
    if(err != ESP_OK) {
        return err;
    }
    FILE *test_file = microsd_open("/selftest/test.txt", "w");
    if (test_file != NULL) {
        const char *msg = "Hello, MicroSD!";
        size_t written = microsd_write(test_file, msg, strlen(msg));
        err = microsd_close(test_file);
        if (err != ESP_OK) {
            return err;
        }
        size_t count = microsd_count_files("/selftest");
        ESP_LOGI(TAG, "Wrote %u bytes to test file. Counted %u files in /selftest", written, count);
    } else {
        ESP_LOGE(TAG, "Failed to open test file for writing");
        return ESP_FAIL;
    }
}

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

    if(TEST) {
        err = microsd_selftest();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Self-test failed: %s", esp_err_to_name(err));
            return err;
        }
    }

    return ESP_OK;
}


