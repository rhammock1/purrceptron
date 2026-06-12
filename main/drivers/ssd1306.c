#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "ssd1306.h"

#define SSD1306_BUFFER_SIZE (SSD1306_WIDTH * SSD1306_HEIGHT / 8)
#define OLED_I2C_TIMEOUT_MS 500

static const char *TAG = "SSD1306";

// represents the physical bus
static i2c_master_bus_handle_t i2c_bus = NULL;
// represents the device on the bus
static i2c_master_dev_handle_t ssd1306_dev = NULL;

static uint8_t transmit_buffer[1 + SSD1306_BUFFER_SIZE]; // 1 byte for control + pixel data
static uint8_t *const framebuffer = transmit_buffer + 1;

static esp_err_t send_cmds(const uint8_t *cmds, size_t len)
{
    uint8_t buf[40];
    if(len + 1 > sizeof(buf)) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = 0x00; // control byte for commands
    memcpy(buf + 1, cmds, len);
    return i2c_master_transmit(ssd1306_dev, buf, len + 1, pdMS_TO_TICKS(OLED_I2C_TIMEOUT_MS));
}

void ssd1306_clear(void)
{
    memset(framebuffer, 0x00, SSD1306_BUFFER_SIZE);
}

esp_err_t ssd1306_flush(void)
{
    esp_err_t err = send_cmds(FLUSH_CMDS, sizeof(FLUSH_CMDS));
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send flush commands: %s", esp_err_to_name(err));
        return err;
    }
    transmit_buffer[0] = 0x40; // control byte for data
    return i2c_master_transmit(ssd1306_dev, transmit_buffer, sizeof(transmit_buffer), pdMS_TO_TICKS(OLED_I2C_TIMEOUT_MS));
}

void ssd1306_draw_pixel(uint8_t x, uint8_t y, bool on)
{
    if(x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) {
        return; // out of bounds
    }
    uint16_t framebuffer_byte_index = (y / 8) * SSD1306_WIDTH + x;
    uint8_t bit_position = 1u << (y & 7); // which row within the page

    if(on) {
        framebuffer[framebuffer_byte_index] |= bit_position;
    } else {
        framebuffer[framebuffer_byte_index] &= ~bit_position;
    }
}

void ssd1306_draw_vline(uint8_t x, uint8_t y0, uint8_t y1)
{
    if(y1 < y0) {
      uint8_t temp = y0;
      y0 = y1;
      y1 = temp;
    }
    for(uint8_t y = y0; y <= y1; y++) {
        ssd1306_draw_pixel(x, y, true);
    }
}

esp_err_t init_ssd1306(void)
{
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = SSD1306_SDA_PIN,
        .scl_io_num = SSD1306_SCL_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_config, &i2c_bus);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_master_probe(i2c_bus, SSD1306_I2C_ADDRESS, pdMS_TO_TICKS(100));
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to find SSD1306 on I2C bus: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "SSD1306 found on I2C bus");

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SSD1306_I2C_ADDRESS,
        .scl_speed_hz = 400000, // 400kHz
    };
    err = i2c_master_bus_add_device(i2c_bus, &dev_config, &ssd1306_dev);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SSD1306 device to I2C bus: %s", esp_err_to_name(err));
        return err;
    }

    err = send_cmds(INIT_CMDS, sizeof(INIT_CMDS));
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send initialization commands to SSD1306: %s", esp_err_to_name(err));
        return err;
    }

    // TEMP
    ssd1306_clear();
    for(uint8_t x = 0; x < SSD1306_WIDTH; x++) {
      // bottom + top edges
      ssd1306_draw_pixel(x, 0, true);
      ssd1306_draw_pixel(x, SSD1306_HEIGHT - 1, true);
    }
    for(uint8_t y = 0; y < SSD1306_HEIGHT; y++) {
      // left + right edges
      ssd1306_draw_pixel(0, y, true);
      ssd1306_draw_pixel(SSD1306_WIDTH - 1, y, true);
    }
    for(uint8_t x = 2; x < SSD1306_WIDTH - 2; x++) {
      // checkerboard pattern
      for(uint8_t y = 2; y < SSD1306_HEIGHT - 2; y++) {
        if(((x ^ y) & 1) == 0) {
          ssd1306_draw_pixel(x, y, true);
        }
      }
    }
    err = ssd1306_flush();
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to flush framebuffer to SSD1306: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "SSD1306 initialized successfully");
    return ESP_OK;
}

