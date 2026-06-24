#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "portmacro.h"
#include "buttons.h"
#include "cats.h"

static const char *TAG = "BUTTON";

#define DEBOUNCE_TIME_MS 250
#define BUTTON_PIN_MASK ( (1ULL << KITTY_LABEL_PIN) | \
                          (1ULL << TODD_LABEL_PIN) | \
                          (1ULL << LADY_LABEL_PIN) | \
                          (1ULL << ROXY_LABEL_PIN) | \
                          (1ULL << RECORD_BUTTON_PIN) )

static QueueHandle_t button_event_queue = NULL;

// last press times for debouncing
static int64_t last_press_time[5] = {0}; // 4 cats + record button

// Map a GPIO pin to an index
static int pin_to_index(uint32_t pin) 
{
    switch(pin) {
        case KITTY_LABEL_PIN:   return 0;
        case TODD_LABEL_PIN:    return 1;
        case LADY_LABEL_PIN:    return 2;
        case ROXY_LABEL_PIN:    return 3;
        case RECORD_BUTTON_PIN: return 4;
        default:                return -1;
    }
}

static void IRAM_ATTR button_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)(intptr_t)arg;
    BaseType_t high_task_woken = pdFALSE;
    xQueueSendFromISR(button_event_queue, &gpio_num, &high_task_woken);
    portYIELD_FROM_ISR(high_task_woken);
}

static void button_task(void *arg)
{
    uint32_t gpio_num;
    for(;;) {
        if(xQueueReceive(button_event_queue, &gpio_num, portMAX_DELAY)) {
            int idx = pin_to_index(gpio_num);
            if(idx < 0) {
                ESP_LOGW(TAG, "Unknown GPIO in button event: %lu", gpio_num);
                continue;
            }
            int64_t now = esp_timer_get_time() / 1000; // convert to ms
            if(now - last_press_time[idx] < DEBOUNCE_TIME_MS) {
                ESP_LOGI(TAG, "Debounced button press on GPIO %lu", gpio_num);
                continue; // debounce
            }
            last_press_time[idx] = now;
            if(gpio_num == RECORD_BUTTON_PIN) {
                toggle_recording();
            } else {
                select_cat((cat_label_t)(idx + 1)); // idx 0-3 maps to CAT_KITTY - CAT_ROXY
            }
        }
    }
}

esp_err_t init_buttons(void)
{
    ESP_LOGI(TAG, "Initializing buttons...");

    // Idle HIGH via internal pull up resistors
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE, // Trigger on falling edge
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = BUTTON_PIN_MASK,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure button GPIOs");
        return err;
    }

    button_event_queue = xQueueCreate(10, sizeof(uint32_t));
    if (button_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create button event queue");
        return ESP_ERR_NO_MEM;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service");
        return err;
    }

    // Attach ISR handlers for each button pin
    gpio_isr_handler_add(KITTY_LABEL_PIN, button_isr_handler, (void *)KITTY_LABEL_PIN);
    gpio_isr_handler_add(TODD_LABEL_PIN, button_isr_handler, (void *)TODD_LABEL_PIN);
    gpio_isr_handler_add(LADY_LABEL_PIN, button_isr_handler, (void *)LADY_LABEL_PIN);
    gpio_isr_handler_add(ROXY_LABEL_PIN, button_isr_handler, (void *)ROXY_LABEL_PIN);
    gpio_isr_handler_add(RECORD_BUTTON_PIN, button_isr_handler, (void *)RECORD_BUTTON_PIN);

    BaseType_t ok = xTaskCreate(button_task, "button_task", 4096, NULL, 10, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Buttons initialized successfully");
    return ESP_OK;
}
