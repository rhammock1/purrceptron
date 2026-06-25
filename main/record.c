#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "esp_log.h"
#include "cats.h"
#include "microsd.h"
#include "inmp441.h"
#include "record.h"

static const char *TAG = "RECORD";

// Pre-roll: the 2s rolling buffer before the record button is pressed
#define PREROLL_SECONDS 2
// 32000 samples = 64KB
#define PREROLL_SAMPLES (PREROLL_SECONDS * SAMPLE_RATE)

// Recording FIFO: deep enough to absorb the 64KB pre-roll buffer + file-open latency + spikes
#define RECORDING_FIFO_BYTES (128 * 1024) // 128KB

static uint16_t clip_counts[CAT_ROXY + 1] = {0};
// Pre-roll ring buffer: written ONLY by the producer task
static int16_t preroll_ring[PREROLL_SAMPLES];
static size_t preroll_head; // next write slot
static size_t preroll_count; // valid samples, saturated at PREROLL_SAMPLES

static StreamBufferHandle_t recording_fifo = NULL;

// Counts chunked dropped because the FIFO was full
static uint32_t fifo_overruns;

// Append n samples to the pre-roll ring buffer, overwriting the oldest
static void preroll_push(const int16_t *samples, size_t n)
{
    for(size_t i = 0; i < n; i++) {
        preroll_ring[preroll_head] = samples[i];
        preroll_head = (preroll_head + 1) % PREROLL_SAMPLES;
    }
    preroll_count += n;
    if(preroll_count > PREROLL_SAMPLES) {
        preroll_count = PREROLL_SAMPLES;
    }
}

// Push the whole pre-roll buffer (oldest -> newest) into the recording FIFO.
// Called once on the record rising edge, from the producer task
static void preroll_flush_to_fifo(void)
{
    size_t start = (preroll_head + (PREROLL_SAMPLES - preroll_count)) % PREROLL_SAMPLES;
    size_t remaining = preroll_count;
    while(remaining > 0) {
        size_t chunk = PREROLL_SAMPLES - start;
        if(chunk > remaining) {
            chunk = remaining;
        }
        xStreamBufferSend(recording_fifo, &preroll_ring[start], chunk * sizeof(int16_t), 0);
        start = (start + chunk) % PREROLL_SAMPLES;
        remaining -= chunk;
    }
}

// Always-on drainer of the mic. RAM only, no SD card writes which could block.
static void record_producer_task(void *arg)
{
    (void)arg;
    int16_t chunk[CHUNK_FRAMES];
    bool was_recording = false;
    for(;;) {
        size_t got = inmp441_get_pcm(chunk, CHUNK_FRAMES, pdMS_TO_TICKS(20));
        bool recording = is_recording();
        if(recording && !was_recording) {
            // Rising edge: drain the pre-roll first
            ESP_LOGI(TAG, "Record button pressed, flushing pre-roll to FIFO");
            preroll_flush_to_fifo();
        }
        was_recording = recording;

        if(got == 0) {
          // No samples available
          continue;
        }
        if(recording) {
            size_t bytes = got * sizeof(int16_t);
            size_t sent = xStreamBufferSend(recording_fifo, chunk, bytes, 0);
            if(sent < bytes) {
                fifo_overruns++;
                ESP_LOGW(TAG, "Recording FIFO overrun: dropped %u bytes", (unsigned)(bytes - sent));
            }
        } else {
            // Not recording, just push to pre-roll
            preroll_push(chunk, got);
        }
    }
}

// TEMPORARY: Drain the FIFO and log how mant samples flow per recording
static void record_temp_drain_task(void *arg)
{
    (void)arg;
    static int16_t drain_buffer[1024];
    uint32_t total_samples = 0;
    bool was_recording = false;
    for(;;) {
        size_t got = xStreamBufferReceive(recording_fifo, drain_buffer, sizeof(drain_buffer), pdMS_TO_TICKS(20));
        bool recording = is_recording();
        if(got > 0) {
            total_samples += got / sizeof(int16_t);
        } else if(!recording && was_recording) {
            // Falling edge: log the total samples recorded
            ESP_LOGI(TAG, "Recording stopped, total samples recorded: %u", (unsigned)total_samples);
            total_samples = 0;
        }
        was_recording = recording;
    }
}

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

    recording_fifo = xStreamBufferCreate(RECORDING_FIFO_BYTES, sizeof(int16_t));
    if(recording_fifo == NULL) {
        ESP_LOGE(TAG, "Failed to create recording FIFO (%d bytes)", RECORDING_FIFO_BYTES);
        return ESP_ERR_NO_MEM;
    }

    // Producer at priority 6 (latency-sensitive)
    // pinned to core 1 so it never competes with the mic's DMA on core 0
    BaseType_t ok = xTaskCreatePinnedToCore(record_producer_task, "record_producer", 4096, NULL, 6, NULL, 1);
    if(ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create record_producer_task");
        return ESP_ERR_NO_MEM;
    }

    // TEMP drain and log consumer
    ok = xTaskCreatePinnedToCore(record_temp_drain_task, "record_temp_drain", 4096, NULL, 5, NULL, 1);
    if(ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create record_temp_drain_task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Record module initialized successfully");
    return ESP_OK;
}
