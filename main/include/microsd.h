#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "esp_err.h"

#define MICROSD_CS_PIN 10
#define MICROSD_MOSI_PIN 11
#define MICROSD_CLK_PIN 12
#define MICROSD_MISO_PIN 13

#define MICROSD_MOUNT_POINT "/sdcard"

// Initialize the SPI bus aknd mount the FAT filesystem
esp_err_t init_microsd(void);

esp_err_t microsd_mkdir(const char *path);
FILE *microsd_open(const char *path, const char *mode);
size_t microsd_write(FILE *file, const void *data, size_t len);
esp_err_t microsd_seek(FILE *file, long offset, int whence);
esp_err_t microsd_close(FILE *file);

size_t microsd_count_files(const char *path);

