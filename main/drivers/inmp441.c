#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "portmacro.h"
#include "inmp441.h"

// INMP441: 24 valid bits MSB aligned in a 32bit word.
// shift right by 16 takes the top 16 bits.
#define RAW_SHIFT 11
#define GAIN 1
#define STREAM_BUFFER_SIZE (CHUNK_FRAMES * sizeof(int16_t) * 4) // enough space for 4 chunks

static const char *TAG = "INMP441";

static i2s_chan_handle_t i2s_rx_channel = NULL;
static StreamBufferHandle_t pcm_stream_buffer = NULL;
// spinlock to protect shared state during mic_rx_task and display_task
static portMUX_TYPE audio_levels_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t audio_levels[MIC_LEVELS_COUNT];
// index of the oldest entry / next write slot
static size_t audio_levels_head;

static void push_level(uint8_t magnitude)
{
    portENTER_CRITICAL(&audio_levels_mux);
    audio_levels[audio_levels_head] = magnitude;
    audio_levels_head = (audio_levels_head + 1) % MIC_LEVELS_COUNT;
    portEXIT_CRITICAL(&audio_levels_mux);
}

size_t inmp441_get_levels(uint8_t *out, size_t n)
{
    if(n > MIC_LEVELS_COUNT) {
        n = MIC_LEVELS_COUNT;
    }
    portENTER_CRITICAL(&audio_levels_mux);
    // Copy the n most-recent entries in chronological order (older-first)
    size_t start = (audio_levels_head + (MIC_LEVELS_COUNT - n)) % MIC_LEVELS_COUNT;
    for(size_t i = 0; i < n; i++) {
        out[i] = audio_levels[(start + i) % MIC_LEVELS_COUNT];
    }
    portEXIT_CRITICAL(&audio_levels_mux);
    return n;
}

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
        // Normalize peak (0..32767) to a 0.255 magnitude and push to the ring
        uint8_t magnitude = (uint8_t)(peak >> 7);
        push_level(magnitude);

        // Pass converted samples to the stream buffer. 
        // If full, the newest samples are dropped
        // timeout of 0 means "don't wait, just drop the data" if the buffer is full, which is what we want to avoid blocking the task and keep it real-time.
        xStreamBufferSend(pcm_stream_buffer, pcm_buffer, n * sizeof(int16_t), 0);
    }
}

size_t inmp441_get_pcm(int16_t *out, size_t max_samples, TickType_t timeout)
{
    if(pcm_stream_buffer == NULL) {
        return 0; // not initialized yet
    }
    size_t bytes = xStreamBufferReceive(pcm_stream_buffer, out, max_samples * sizeof(int16_t), timeout);
    return bytes / sizeof(int16_t);
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

    pcm_stream_buffer = xStreamBufferCreate(STREAM_BUFFER_SIZE, sizeof(int16_t));
    if(pcm_stream_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to create PCM stream buffer");
        return ESP_ERR_NO_MEM;
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

