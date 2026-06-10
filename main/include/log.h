#pragma once

#include "esp_log.h"

static inline void logi(const char *TAG, const char *message)
{
    ESP_LOGI(TAG, "%s", message);
}

static inline void loge(const char *TAG, const char *message)
{
    ESP_LOGE(TAG, "%s", message);
}
