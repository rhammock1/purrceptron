#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "inmp441.h"

static const char *TAG = "INMP441";

static i2s_chan_handle_t i2s_rx_channel = NULL;

esp_err_t init_inmp441(void)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel_config.dma_desc_num = 6;
    channel_config.dma_frame_num = CHUNK_FRAMES;

    esp_err_t err = i2s_new_channel(&channel_config, NULL, &i2s_rx_channel);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(err));
        return err;
    }

    // INMP441 uses standard i2s (Philips) timing
    // 24-bit data in a 32-bit slot
    // L/R pin tied to GND so all data is in the left channel
    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT, 
            I2S_SLOT_MODE_MONO
        ),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = INMP441_CLK_PIN,
            .ws = INMP441_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din = INMP441_DATA_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    err = i2s_channel_init_std_mode(i2s_rx_channel, &std_config);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S channel in standard mode: %s", esp_err_to_name(err));
        return err;
    }

    err = i2s_channel_enable(i2s_rx_channel);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(err));
        return err;
    }

    // TEMP
    int32_t raw_buffer[CHUNK_FRAMES];
    size_t bytes_read;
    err = i2s_channel_read(i2s_rx_channel, raw_buffer, sizeof(raw_buffer), &bytes_read, portMAX_DELAY);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read from I2S channel: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Read %u bytes; raw[0..3] = %ld, %ld, %ld, %ld", (unsigned)bytes_read, (long)raw_buffer[0], (long)raw_buffer[1], (long)raw_buffer[2], (long)raw_buffer[3]);

    ESP_LOGI(TAG, "INMP441 initialized successfully");
    return ESP_OK;
}

