#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "inmp441.h"

// INMP441: 24 valid bits MSB aligned in a 32bit word.
// shift right by 16 takes the top 16 bits.
#define RAW_SHIFT 16
#define GAIN 1

static const char *TAG = "INMP441";

static i2s_chan_handle_t i2s_rx_channel = NULL;

static int16_t convert_sample(int32_t sample)
{
    int32_t shifted = (sample >> RAW_SHIFT) * GAIN;
    if(shifted > INT16_MAX) {
        shifted = INT16_MAX;
    } else if(shifted < INT16_MIN) {
        shifted = INT16_MIN;
    }
    return (int16_t)shifted;
}

static void inmp441_rx_task(void *arg)
{
    (void)arg;
    int32_t raw_buffer[CHUNK_FRAMES];
    int16_t pcm_buffer[CHUNK_FRAMES];
    uint32_t log_throttle = 0;

    for(;;) {
        size_t bytes_read = 0;
        esp_err_t err = i2s_channel_read(i2s_rx_channel, raw_buffer, sizeof(raw_buffer), &bytes_read, portMAX_DELAY);
        if(err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read from I2S channel: %s", esp_err_to_name(err));
            continue;
        }
        size_t n = bytes_read / sizeof(int32_t);

        int32_t peak = 0;
        for(size_t i = 0; i < n; i++) {
            int16_t pcm_sample = convert_sample(raw_buffer[i]);
            pcm_buffer[i] = pcm_sample;
            int32_t abs_sample = pcm_sample < 0 ? -(int32_t)pcm_sample : (int32_t)pcm_sample; // convert to absolute value to avoid int16_min overflow
            if(abs_sample > peak) {
                peak = abs_sample;
            }
        }

        // TEMP: log the peak ~4x/sec
        if((log_throttle++ & 0x0F) == 0) {
            ESP_LOGI(TAG, "Read %u bytes, peak sample = %ld", (unsigned)n, (long)peak);
        }
    }
}

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

    // Always-on reader, pinner to core 0 so DMA servicing
    // never competes with the OLED's I2C flush.
    BaseType_t ok = xTaskCreatePinnedToCore(inmp441_rx_task, "inmp441_rx_task", 4096, NULL, 6, NULL, 0);
    if(ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create INMP441 RX task");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "INMP441 initialized successfully. Started RX task on core 0");
    return ESP_OK;
}

