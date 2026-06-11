#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "ssd1306.h"

static const char *TAG = "SSD1306";

// represents the physical bus
static i2c_master_bus_handle_t i2c_bus = NULL;
// represents the device on the bus
static i2c_master_dev_handle_t ssd1306_dev = NULL;

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

    ESP_LOGI(TAG, "SSD1306 initialized successfully");
    return ESP_OK;
}
