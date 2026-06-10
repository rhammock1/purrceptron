#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "buttons.h"
#include "log.h"

static const char *TAG = "BUTTON";

#define DEBOUNCE_TIME_MS 50

static const char *CAT_NAMES[] = {
   [CAT_NONE] = "None",
   [CAT_KITTY] = "Kitty",
   [CAT_TODD] = "Todd",
   [CAT_LADY] = "Lady",
   [CAT_ROXY] = "Roxy",
};

// state variables
static cat_label_t selected_cat = CAT_NONE;
static bool recording = false;
// last press times for debouncing
static int64_t last_press_time[5] = {0}; // 4 cats + record button

esp_err_t init_buttons(void)
{
    logi(TAG, "Initializing buttons...");
    // Initialize GPIO pins for buttons
    // Configure interrupts for button presses
    return ESP_OK;
}

cat_label_t get_selected_cat(void)
{
   if (selected_cat < CAT_NONE || selected_cat > CAT_ROXY) {
      return CAT_NONE;
   }
    return selected_cat;
}

bool is_recording(void)
{
    return recording;
}

const char *cat_label_to_string(cat_label_t label)
{
   if (label < CAT_NONE || label > CAT_ROXY) {
      return "Unknown";
   }
    return CAT_NAMES[label];
}
