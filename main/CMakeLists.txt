idf_component_register(
    SRCS "purrceptron.c"
          "ui.c"
         "drivers/buttons.c"
         "drivers/ssd1306.c"
         "drivers/inmp441.c"
    INCLUDE_DIRS "." "include"
)
