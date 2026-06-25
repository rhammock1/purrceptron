#include "esp_log.h"
#include "cats.h"

static const char *TAG = "CATS";

static const char *CAT_NAMES[] = {
   [CAT_NONE] = "None",
   [CAT_KITTY] = "Kitty",
   [CAT_TODD] = "Todd",
   [CAT_LADY] = "Lady",
   [CAT_ROXY] = "Roxy",
};

// Lowercase folder names for each cat label
const char *CAT_FOLDER_LABELS[] = {
    [CAT_KITTY] = "kitty",
    [CAT_TODD] = "todd",
    [CAT_LADY] = "lady",
    [CAT_ROXY] = "roxy"
};

// state variables
static bool recording = false;
static cat_label_t selected_cat = CAT_NONE;

bool is_recording(void)
{
    return recording;
}

cat_label_t get_selected_cat(void)
{
   if (selected_cat < CAT_NONE || selected_cat > CAT_ROXY) {
      return CAT_NONE;
   }
    return selected_cat;
}

const char *cat_label_to_string(cat_label_t label)
{
   if (label < CAT_NONE || label > CAT_ROXY) {
      return "Unknown";
   }
    return CAT_NAMES[label];
}

void select_cat(cat_label_t label)
{
    if(recording) {
        ESP_LOGW(TAG, "Cannot change label while recording. Stop recording first.");
        return;
    } else if(selected_cat == label) {
        ESP_LOGI(TAG, "Label already selected: %s, setting to NONE", cat_label_to_string(label));
        selected_cat = CAT_NONE;
        return;
    }
    selected_cat = label;
    ESP_LOGI(TAG, "Selected cat: %s", cat_label_to_string(label));
}

void toggle_recording(void)
{
    if(selected_cat == CAT_NONE) {
        ESP_LOGW(TAG, "No cat selected, cannot start recording");
        return;
    }
    recording = !recording;
    if(recording) {
        ESP_LOGI(TAG, "Recording STARTED for cat: %s", cat_label_to_string(selected_cat));
    } else {
        ESP_LOGI(TAG, "Recording STOPPED for cat: %s", cat_label_to_string(selected_cat));
    }
}

