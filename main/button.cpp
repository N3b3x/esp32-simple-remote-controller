/**
 * @file button.cpp
 * @brief Button handling implementation
 */

#include "button.hpp"
#include "config.hpp"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio_filter.h"

static const char* TAG_BTN_ = "Buttons";

static QueueHandle_t s_btn_queue_ = nullptr;
static volatile int64_t s_last_back_time_ = 0;
static volatile int64_t s_last_confirm_time_ = 0;

struct ButtonContext {
    ButtonId id;
    gpio_num_t pin;
};

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    ButtonContext* ctx = (ButtonContext*)arg;
    
    // Verify button is actually pressed (LOW)
    // This filters out rising edge interrupts if they occur
    if (gpio_get_level(ctx->pin) != 0) {
        return;
    }

    int64_t now = esp_timer_get_time(); // microseconds
    volatile int64_t* last_time_ptr = (ctx->id == ButtonId::Back) ? &s_last_back_time_ : &s_last_confirm_time_;

    // Debounce check (convert ms to us)
    if ((now - *last_time_ptr) > (BUTTON_DEBOUNCE_MS_ * 1000)) {
        *last_time_ptr = now;
        
        ButtonEvent ev{ ctx->id };
        BaseType_t hpw = pdFALSE;
        xQueueSendFromISR(s_btn_queue_, &ev, &hpw);
        if (hpw == pdTRUE) portYIELD_FROM_ISR();
    }
}

bool Buttons::Init(QueueHandle_t evt_queue) noexcept
{
    s_btn_queue_ = evt_queue;

    gpio_config_t io_conf{};
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.intr_type = GPIO_INTR_NEGEDGE; // assuming buttons to GND, pull-ups
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;

    // Configure OLED UI board buttons (BACK and CONFIRM only)
    // Encoder Push button is handled by EC11Encoder component
    io_conf.pin_bit_mask = (1ULL << BTN_BACK_GPIO_) |
                           (1ULL << BTN_CONFIRM_GPIO_);
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Enable hardware glitch filter for buttons
    gpio_glitch_filter_handle_t filter_handle;
    gpio_pin_glitch_filter_config_t filter_conf = {
        .clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT,
        .gpio_num = BTN_BACK_GPIO_
    };
    if (gpio_new_pin_glitch_filter(&filter_conf, &filter_handle) == ESP_OK) {
        gpio_glitch_filter_enable(filter_handle);
        ESP_LOGI(TAG_BTN_, "Glitch filter enabled for BACK button");
    }

    filter_conf.gpio_num = BTN_CONFIRM_GPIO_;
    if (gpio_new_pin_glitch_filter(&filter_conf, &filter_handle) == ESP_OK) {
        gpio_glitch_filter_enable(filter_handle);
        ESP_LOGI(TAG_BTN_, "Glitch filter enabled for CONFIRM button");
    }

    // Encoder push button glitch filter is handled by EC11Encoder

    // Install ISR service, ignoring error if already installed (though this runs first)
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    // OLED UI board buttons
    static ButtonContext back_ctx = { ButtonId::Back, BTN_BACK_GPIO_ };
    static ButtonContext confirm_ctx = { ButtonId::Confirm, BTN_CONFIRM_GPIO_ };
    
    ESP_ERROR_CHECK(gpio_isr_handler_add(BTN_BACK_GPIO_, gpio_isr_handler, &back_ctx));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BTN_CONFIRM_GPIO_, gpio_isr_handler, &confirm_ctx));
    
    ESP_LOGI(TAG_BTN_, "Buttons initialized");
    return true;
}

void Buttons::ConfigureWakeup() noexcept
{
    // BACK and CONFIRM as EXT1 wake sources (any low).
    // Encoder push (GPIO22) is not RTC-capable on ESP32-C6, so do NOT include it.
    uint64_t mask = (1ULL << BTN_BACK_GPIO_) |
                    (1ULL << BTN_CONFIRM_GPIO_);

    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
    // NOTE: Only RTC-capable pins should be used here to avoid wake errors.
}

