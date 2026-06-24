#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "buttons.h"
#include "microsd.h"
#include "record.h"

static const char *TAG = "RECORD";

// Lowercase folder names for each cat label
static const char *CAT_FOLDER_LABELS[] = {
    [CAT_KITTY] = "kitty",
    [CAT_TODD] = "todd",
    [CAT_LADY] = "lady",
    [CAT_ROXY] = "roxy"
};

static uint16_t clip_counts[CAT_ROXY + 1] = {0};

uint16_t record_get_clip_count(cat_label_t cat)
{
    if (cat < CAT_KITTY || cat > CAT_ROXY) {
        ESP_LOGW(TAG, "Invalid cat label: %d", cat);
        return 0;
    }
    return clip_counts[cat];
}

esp_err_t init_record(void)
{
    // Seed clip counters 
    for (cat_label_t cat = CAT_KITTY; cat <= CAT_ROXY; cat++) {
        char dir_path[16];
        snprintf(dir_path, sizeof(dir_path), "/%s", CAT_FOLDER_LABELS[cat]);
        clip_counts[cat] = (uint16_t)microsd_count_files(dir_path);
        ESP_LOGI(TAG, "Found %u clips for cat %s in directory %s", clip_counts[cat], CAT_FOLDER_LABELS[cat], dir_path);
    }

    ESP_LOGI(TAG, "Record module initialized successfully");
    return ESP_OK;
}
