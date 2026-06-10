#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Cat label button pins
#define KITTY_LABEL_PIN 18
#define TODD_LABEL_PIN 17
#define LADY_LABEL_PIN 16
#define ROXY_LABEL_PIN 15

// Start/Stop Recording button pin
#define RECORD_BUTTON_PIN 14

// Which cat is currently selected for labeling
typedef enum {
    CAT_NONE = 0,
    CAT_KITTY,
    CAT_TODD,
    CAT_LADY,
    CAT_ROXY
} cat_label_t;

esp_err_t init_buttons(void);

cat_label_t get_selected_cat(void);

bool is_recording(void);

const char *cat_label_to_string(cat_label_t label);

