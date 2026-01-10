/**
 * @file ui_controller.hpp
 * @brief Main UI controller for remote controller
 */

#pragma once

#include <memory>
#include <cstdint>
#include "ui_state.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "../button.hpp"
#include "../protocol/espnow_protocol.hpp"
#include "../settings.hpp"
#include "../components/Adafruit_SH1106_ESPIDF/Adafruit_SH1106.h"
#include "../components/EC11_Encoder/inc/ec11_encoder.hpp"
#include "../devices/device_base.hpp"

class UiController {
public:
    // Public functions: PascalCase
    bool Init(QueueHandle_t ui_queue, Settings* settings, 
              uint32_t* inactivity_ticks_ptr) noexcept;
    void Task(void* arg) noexcept;
    void PrepareForSleep() noexcept;
    
private:
    // Private functions: camelCase
    void handleButton(const ButtonEvent& event) noexcept;
    void handleProtocol(const espnow::ProtoEvent& event) noexcept;
    void handleEncoderButton(bool pressed) noexcept;
    void renderCurrentScreen() noexcept;
    void transitionToState(UiState new_state) noexcept;
    void renderSplashScreen() noexcept;
    void renderDeviceSelectionScreen() noexcept;
    void renderDeviceMainScreen() noexcept;
    void renderDeviceSettingsScreen() noexcept;
    void renderDeviceControlScreen() noexcept;
    void renderPopup() noexcept;
    
    // Encoder helper methods
    void resetEncoderTracking(int32_t position = 0) noexcept;
    void processEncoderEvents() noexcept;
    
    // Member variables: snake_case + trailing underscore
    UiState current_state_;
    std::unique_ptr<DeviceBase> current_device_;
    QueueHandle_t ui_queue_;
    Settings* settings_;
    uint32_t* last_activity_tick_;
    uint8_t selected_device_id_;
    bool popup_active_;
    
    // Encoder tracking (moved from Task() local variables for proper state sync)
    bool last_encoder_button_state_;
    int32_t last_encoder_pos_;
};

