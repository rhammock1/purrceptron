#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "buttons.h" // cat_label_t

// Call after init_microsd and init_inmp441 so that the SD card is mounted and mic is ready to record.
// Scans /{cat}/ folders to seed clip counters
// Fatal at the call site
esp_err_t init_record(void);

// called by ui.c's get_clip_count() 
uint16_t record_get_clip_count(cat_label_t cat);

