#pragma once

#include "cats.h"
#include "ssd1306.h"
#include "inmp441.h"
#include "microsd.h"

uint8_t get_clip_count(cat_label_t label);
void get_audio_levels(uint8_t *out, size_t n);
void display_task(void *arg);
