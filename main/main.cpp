/**
 * @file main.cpp
 * @brief Main application for remote controller
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"

#include "config.hpp"
#include "protocol/espnow_protocol.hpp"
#include "button.hpp"
#include "settings.hpp"
#include "ui/ui_controller.hpp"

static const char* TAG_MAIN_ = "Main";

QueueHandle_t g_button_queue_ = nullptr;
QueueHandle_t g_proto_queue_  = nullptr;
static QueueHandle_t g_ui_queue_     = nullptr;

// Used for inactivity->deep sleep
static uint32_t g_last_activity_tick_ = 0;

// Global settings instance (MUST be static/global because app_main returns)
static Settings g_settings;

// Track boot time for wake-up debouncing
static TickType_t g_boot_tick_ = 0;

// Forward tasks
static void button_task(void* arg);
static void proto_task(void* arg);
static void power_task(void* arg);

static UiController g_ui_controller;

extern "C" void app_main(void)
{
    g_boot_tick_ = xTaskGetTickCount();
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG_MAIN_, "Boot, wakeup cause: %d", (int)wakeup_cause);

    // Initialize settings from NVS
    SettingsStore::Init(g_settings);

    g_button_queue_ = xQueueCreate(10, sizeof(ButtonEvent));
    g_proto_queue_  = xQueueCreate(10, sizeof(espnow::ProtoEvent));
    g_ui_queue_     = xQueueCreate(10, sizeof(ButtonEvent)); // Store button events directly

    // Init ESPNOW
    espnow::Init(g_proto_queue_);

    // Buttons (ISR->g_button_queue_)
    Buttons::Init(g_button_queue_);

    // Configure deep sleep wake from buttons
    Buttons::ConfigureWakeup();

    // Initialize UI controller
    g_last_activity_tick_ = xTaskGetTickCount();
    if (!g_ui_controller.Init(g_ui_queue_, &g_settings, &g_last_activity_tick_)) {
        ESP_LOGE(TAG_MAIN_, "Failed to initialize UI controller");
        return;
    }

    // Launch tasks
    xTaskCreate(button_task, "button_task", 4096, nullptr, 6, nullptr);
    xTaskCreate(proto_task,  "proto_task",  4096, nullptr, 5, nullptr);
    xTaskCreate([](void* arg) { g_ui_controller.Task(arg); }, "ui_task", 8192, nullptr, 4, nullptr);
    xTaskCreate(power_task,  "power_task",  4096, nullptr, 3, nullptr);
}

// Button task: forward button events into UI queue
static void button_task(void* arg)
{
    (void)arg;
    ButtonEvent be{};
    const TickType_t debounce_time = pdMS_TO_TICKS(2000); // 2 second debounce after wake
    
    while (true) {
        if (xQueueReceive(g_button_queue_, &be, portMAX_DELAY) == pdTRUE) {
            // Ignore button events for 2 seconds after waking from sleep
            TickType_t now = xTaskGetTickCount();
            if (now - g_boot_tick_ < debounce_time) {
                ESP_LOGI(TAG_MAIN_, "Button ignored (wake-up debounce): %d ms since boot", 
                         (int)pdTICKS_TO_MS(now - g_boot_tick_));
                continue;
            }
            
            // Forward button event directly to UI queue
            xQueueSend(g_ui_queue_, &be, portMAX_DELAY);
        }
    }
}

// Proto task: protocol events are handled directly in UI controller task
// (no forwarding needed - UI controller polls g_proto_queue_ directly)
static void proto_task(void* arg)
{
    (void)arg;
    // Protocol events are handled directly in UI controller's main loop
    // This task is kept for future use if needed
    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
}

// Power task: check inactivity and enter deep sleep
static void power_task(void* arg)
{
    (void)arg;
    const TickType_t check_period = pdMS_TO_TICKS(1000);
    while (true) {
        vTaskDelay(check_period);

        TickType_t now = xTaskGetTickCount();
        TickType_t timeout_ticks = pdMS_TO_TICKS(INACTIVITY_TIMEOUT_SEC_ * 1000);
        if (now - g_last_activity_tick_ > timeout_ticks) {
            ESP_LOGI(TAG_MAIN_, "Inactivity timeout reached, entering deep sleep");

            // Prepare UI for sleep
            g_ui_controller.PrepareForSleep();

            vTaskDelay(pdMS_TO_TICKS(100));
            esp_deep_sleep_start();
        }
    }
}

