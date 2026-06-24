#pragma once

#include <stdbool.h>

// Which cat is currently selected for labeling
typedef enum {
    CAT_NONE = 0,
    CAT_KITTY,
    CAT_TODD,
    CAT_LADY,
    CAT_ROXY
} cat_label_t;

cat_label_t get_selected_cat(void);

const char *cat_label_to_string(cat_label_t label);

bool is_recording(void);

void select_cat(cat_label_t label);
void toggle_recording(void);

