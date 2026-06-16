#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#define INMP441_CLK_PIN 4
#define INMP441_WS_PIN 5
#define INMP441_DATA_PIN 6

#define SAMPLE_RATE 16000 // hz, mono
#define CHUNK_FRAMES  256 // samples per i2s dma read
#define MIC_LEVELS_COUNT 128 // one peak per OLED column 

esp_err_t init_inmp441(void);
size_t inmp441_get_levels(uint8_t *out, size_t n); // fills the provided buffer with audio level data for a simple VU meter visualization on the OLED while recording, returns the number of levels written to the buffer
size_t inmp441_get_pcm(int16_t *out, size_t max_samples, TickType_t timeout); // drain up to max_samples of raw PCM into out, returns the number of samples read, waits up to timeout if no samples are immediately available


