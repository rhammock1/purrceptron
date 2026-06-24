idf_component_register(
    SRCS "purrceptron.c"
          "ui.c"
          "record.c"
         "drivers/buttons.c"
         "drivers/ssd1306.c"
         "drivers/inmp441.c"
         "drivers/microsd.c"
    INCLUDE_DIRS "." "include"
)
