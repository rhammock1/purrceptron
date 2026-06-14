#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#define INMP441_CLK_PIN 4
#define INMP441_WS_PIN 5
#define INMP441_DATA_PIN 6

#define SAMPLE_RATE 16000 // hz, mono
#define CHUNK_FRAMES  256 // samples per i2s dma read

esp_err_t init_inmp441(void);

