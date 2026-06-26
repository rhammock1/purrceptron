#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include <string.h>
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

#define WAV_HEADER_SIZE 44
#define WRITE_BLOCK_SAMPLES 1024 // ~2KB writes

static uint16_t clip_counts[CAT_ROXY + 1] = {0};
// Pre-roll ring buffer: written ONLY by the producer task
static int16_t preroll_ring[PREROLL_SAMPLES];
static size_t preroll_head; // next write slot
static size_t preroll_count; // valid samples, saturated at PREROLL_SAMPLES

static StreamBufferHandle_t recording_fifo = NULL;

// Counts chunked dropped because the FIFO was full
static uint32_t fifo_overruns;

// ensure that the WAV header is written in little-endian format
static void write_u16le(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
}

static void write_u32le(uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
    buf[2] = (uint8_t)((val >> 16) & 0xFF);
    buf[3] = (uint8_t)((val >> 24) & 0xFF);
}

// Build a standard 44-byte PCM WAV header
// 16kHz, mono, 16-bit samples
// data_bytes is the PCM payload size (0 for the placeholder, the real total when patching on file close)
static void build_wav_header(uint8_t *header, uint32_t data_bytes)
{
    const uint32_t sample_rate = SAMPLE_RATE;
    const uint16_t channels = 1;
    const uint16_t bits = 16;
    // how many bytes make up one sample frame
    const uint16_t block_align = channels * (bits / 8); // 2
    const uint32_t byte_rate = sample_rate * block_align; // 32000

    memcpy(header + 0, "RIFF", 4); // ChunkID
    write_u32le(header + 4, 36 + data_bytes); // ChunkSize
    memcpy(header + 8, "WAVE", 4); // Format
    memcpy(header + 12, "fmt ", 4); // Subchunk1ID
    write_u32le(header + 16, 16); // Subchunk1Size (typically 16 for PCM)
    write_u16le(header + 20, 1); // AudioFormat (1 = PCM)
    write_u16le(header + 22, channels); // NumChannels
    write_u32le(header + 24, sample_rate); // SampleRate
    write_u32le(header + 28, byte_rate); // ByteRate
    write_u16le(header + 32, block_align); // BlockAlign
    write_u16le(header + 34, bits); // BitsPerSample
    memcpy(header + 36, "data", 4); // Subchunk2ID
    write_u32le(header + 40, data_bytes); // Subchunk2Size
}

// Create /{cat}/NNNN.wav and write the placeholder header.
// Returns NULL on any SD error (caller should stay idle and retry)
static FILE *open_new_clip(cat_label_t cat)
{
    const char *folder = CAT_FOLDER_LABELS[cat];
    char dir_path[16];
    snprintf(dir_path, sizeof(dir_path), "/%s", folder);
    if(microsd_mkdir(dir_path) != ESP_OK) {
        return NULL;
    }

    char path[40];
    snprintf(path, sizeof(path), "/%s/%04u.wav", folder, (unsigned)clip_counts[cat]);
    FILE *file = microsd_open(path, "wb");
    if(file == NULL) {
        return NULL;
    }

    uint8_t header[WAV_HEADER_SIZE];
    build_wav_header(header, 0); // placeholder, will patch on close
    if(microsd_write(file, header, WAV_HEADER_SIZE) < WAV_HEADER_SIZE) {
        microsd_close(file);
        return NULL;
    }
    ESP_LOGI(TAG, "Opened new clip file: %s", path);
    return file;
}

static void finalize_clip(FILE *file, cat_label_t cat, uint32_t data_bytes)
{
    uint8_t header[WAV_HEADER_SIZE];
    build_wav_header(header, data_bytes);
    if(microsd_seek(file, 0, SEEK_SET) == ESP_OK) {
        microsd_write(file, header, WAV_HEADER_SIZE);
    } else {
        ESP_LOGW(TAG, "Failed to seek back to start of file to patch WAV header");
    }
    microsd_close(file);
    clip_counts[cat]++;
    ESP_LOGI(TAG, "Finalized clip for cat %s, total samples: %u, total clips: %u", CAT_FOLDER_LABELS[cat], (unsigned)(data_bytes / sizeof(int16_t)), (unsigned)clip_counts[cat]);
}

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

// Owns all SD I/O.
static void record_writer_task(void *arg)
{
    (void)arg;
    static uint8_t block[WRITE_BLOCK_SAMPLES * sizeof(int16_t)];
    FILE *file = NULL;
    cat_label_t cat = CAT_NONE;
    uint32_t total_samples = 0;
    for(;;) {
        bool recording = is_recording();
        if(recording && file == NULL) {
            cat = get_selected_cat();
            if(cat == CAT_NONE) { // sanity check, should never happen
                ESP_LOGW(TAG, "Record button pressed but no cat selected");
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            file = open_new_clip(cat);
            total_samples = 0;
            if(file == NULL) {
                ESP_LOGE(TAG, "Failed to open new clip file for cat %s", CAT_FOLDER_LABELS[cat]);
                vTaskDelay(pdMS_TO_TICKS(50)); // retry while still recording
                continue;
            }
        }
        if(file == NULL) {
            // Not recording, just wait a bit
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t got = xStreamBufferReceive(recording_fifo, block, sizeof(block), pdMS_TO_TICKS(20));
        if(got > 0) {
            size_t written = microsd_write(file, block, got);
            total_samples += written;
            if(written < got) {
                // write fault, abort the clip
                ESP_LOGW(TAG, "Failed to write all bytes to SD card: wrote %u of %u", (unsigned)written, (unsigned)got);
                microsd_close(file);
                file = NULL;
            }
        } else if(!recording) {
            // Falling edge, finalize the clip
            finalize_clip(file, cat, total_samples);
            file = NULL;
        }
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

    ok = xTaskCreatePinnedToCore(record_writer_task, "record_writer", 4096, NULL, 5, NULL, 1);
    if(ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create record_writer_task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Record module initialized successfully");
    return ESP_OK;
}
