#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cats.h"
#include "record.h"
#include "ui.h"

static const char *TAG = "UI";

#define WAVE_CENTER_Y 40 // baseline row of the waveform region
#define WAVE_MAX_AMP 23 // max half-height (40 +/- 23 = 17..63) for the waveform display on the OLED, leaving some headroom at the top and bottom

uint8_t get_clip_count(cat_label_t label)
{
    return (uint8_t)record_get_clip_count(label);
}


void get_audio_levels(uint8_t *out, size_t n)
{
    uint8_t mag[SSD1306_WIDTH];
    size_t want = n < SSD1306_WIDTH ? n : SSD1306_WIDTH;
    size_t got = inmp441_get_levels(mag, want);
    for(size_t i = 0; i < got; i++) {
        out[i] = (uint8_t)(((uint32_t)mag[i] * WAVE_MAX_AMP) / 255);
    }
    for(size_t i = got; i < n; i++) {
        out[i] = 0;
    }
}

void display_task(void *arg)
{
    (void)arg;
    char line[24];
    uint8_t levels[SSD1306_WIDTH];

    for(;;) {
        ssd1306_clear();

        // Status line: armed cat + recording state
        cat_label_t cat = get_selected_cat();
        snprintf(line, sizeof(line), "Armed: %s", cat_label_to_string(cat));
        ssd1306_draw_text(0, 0, line);
        if(is_recording()) {
            ssd1306_draw_text(104, 0, "REC");
        }
        if(record_get_fifo_overruns() > 0) {
            // fifo overrun warning
            ssd1306_draw_text(96, 0, "!");
        }

        // Counts line: per-cat clip counts
        snprintf(line, sizeof(line), "K:%u T:%u L:%u R:%u", 
            get_clip_count(CAT_KITTY),
            get_clip_count(CAT_TODD),
            get_clip_count(CAT_LADY),
            get_clip_count(CAT_ROXY));
        ssd1306_draw_text(0, 8, line);

        // Waveform: one centered vertical bar per column
        get_audio_levels(levels, SSD1306_WIDTH);
        for(uint8_t x = 0; x < SSD1306_WIDTH; x++) {
            uint8_t amp = levels[x] > WAVE_MAX_AMP ? WAVE_MAX_AMP : levels[x];
            ssd1306_draw_vline(x, WAVE_CENTER_Y - amp, WAVE_CENTER_Y + amp);
        }

        esp_err_t err = ssd1306_flush();
        if(err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to flush display in display_task: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(70)); // ~14 fps
    }
}

