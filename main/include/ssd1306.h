#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define SSD1306_I2C_ADDRESS 0x3C
#define SSD1306_SDA_PIN 8
#define SSD1306_SCL_PIN 9
#define SSD1306_WIDTH 128
#define SSD1306_HEIGHT 64

static const uint8_t INIT_CMDS[] = {
    0xAE,        // display off
    0xD5, 0x80,  // clock divide ratio / osc freq
    0xA8, 0x3F,  // multiplex ratio 1/64
    0xD3, 0x00,  // display offset 0
    0x40,        // start line 0
    0x8D, 0x14,  // charge pump ON (omit this == blank screen)
    0x20, 0x00,  // memory addressing mode: horizontal
    0xA1,        // segment remap (col 127 -> SEG0) -> not mirrored L/R
    0xC8,        // COM output scan direction remapped -> not upside down
    0xDA, 0x12,  // COM pins config (alt, 64-row panel)
    0x81, 0xCF,  // contrast
    0xD9, 0xF1,  // pre-charge period
    0xDB, 0x40,  // VCOMH deselect level
    0xA4,        // output follows RAM content
    0xA6,        // normal display (not inverted)
    0xAF,        // display ON
};

static const uint8_t FLUSH_CMDS[] = {
    0x21, 0x00, 0x7F,  // column address 0..127
    0x22, 0x00, 0x07,  // page address 0..7
};

esp_err_t init_ssd1306(void);

void ssd1306_clear(void);
esp_err_t ssd1306_flush(void);
void ssd1306_draw_pixel(uint8_t x, uint8_t y, bool on);
void ssd1306_draw_vline(uint8_t x, uint8_t y0, uint8_t y1);


