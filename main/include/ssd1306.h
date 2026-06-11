#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define SSD1306_I2C_ADDRESS 0x3C
#define SSD1306_SDA_PIN 8
#define SSD1306_SCL_PIN 9
#define SSD1306_WIDTH 128
#define SSD1306_HEIGHT 64

esp_err_t init_ssd1306(void);

