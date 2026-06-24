#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "buttons.h"
#include "ssd1306.h"
#include "inmp441.h"
#include "microsd.h"
#include "ui.h"
#include "record.h"

static const char *TAG = "PURRCEPTRON";

/*
  This project is to build a simple machine learning algorithm that can classify
  meow sounds from my four cats: Kitty, Todd, Lady, and Roxy.
  We will capture audio samples from each cat, label them using buttons, and then 
  train a model to predict which cat is meowing based on the audio input.

  The system will use: 
    a INMP441 microphone to capture audio
    a SSD1306 OLED display to show the predicted cat name
    an SD card to store the captured audio for training
    four buttons to label the captured audio for training
    one button to start/stop recording

  The expected flow is:
    1. To press a label button for the desired cat to record
      - See the label on the OLED
      - Start a 2-second rolling buffer to capture audio
    2. Press the record button to start recording
      - Save the audio to the SD card in chunks with the cat label as the folder and timestamp as the filename
      - Audio should be saved in WAV and should include the pre-roll buffer
    3. Press the record button again to stop recording

  We will need to configure an interrupt for the label buttons and create a simple state-machine for the recording button.
*/

void init_tasks(void)
{
    ESP_LOGI(TAG, "Initializing tasks...");
    // Initialize your tasks here
    ESP_ERROR_CHECK(init_buttons());
    ESP_ERROR_CHECK(init_inmp441());
    ESP_ERROR_CHECK(init_microsd());
    ESP_ERROR_CHECK(init_record());

    if(init_ssd1306() == ESP_OK) { // optional peripheral
        ESP_LOGI(TAG, "SSD1306 initialized successfully in main");
        // Pin to core 1 so the ~23ms I2C flush doesn't compete with future
        // I2S/audio processing that we'll pin to core 0.
        xTaskCreatePinnedToCore(display_task, "display_task", 4096, NULL, 5, NULL, 1);
        ESP_LOGI(TAG, "Started display task on core 1");
        // ssd1306_start_bounce_demo(); // TEMP: fun to watch while building out the rest
    } else {
        ESP_LOGW(TAG, "Failed to initialize SSD1306 in main");
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting project...");
    init_tasks();
}
