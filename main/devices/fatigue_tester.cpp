/**
 * @file fatigue_tester.cpp
 * @brief Fatigue test device implementation with full menu and screen support
 */

#include "fatigue_tester.hpp"
#include "../protocol/espnow_protocol.hpp"
#include "../protocol/device_protocols.hpp"
#include "../devices/device_registry.hpp"
#include "../settings.hpp"
#include "../components/EC11_Encoder/inc/ec11_encoder.hpp"
#include "../menu/menu_system.hpp"
#include "../menu/menu_items.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>

static const char* TAG_ = "FatigueTester";

FatigueTester::FatigueTester(Adafruit_SH1106* display, Settings* settings) noexcept
    : DeviceBase(display, settings)
    , current_state_(device_protocols::FatigueTestState::Idle)
    , current_cycle_(0)
    , error_code_(0)
    , popup_active_(false)
    , popup_type_(0)
    , popup_yes_selected_(false)
    , settings_synced_(false)
    , menu_active_(false)
    , menu_selected_index_(0)
    , editing_value_(false)
    , editing_choice_(false)
    , menu_edit_step_(1)
    , error_count_(0)
    , confirm_hold_start_(0)
    , confirm_held_(false)
{
    // Initialize error array
    for (size_t i = 0; i < MAX_ERRORS_; ++i) {
        errors_[i] = {0, 0, 0};
    }
    
    // Request initial config from device
    RequestStatus();
}

uint8_t FatigueTester::GetDeviceId() const noexcept
{
    return device_registry::DEVICE_ID_FATIGUE_TESTER_;
}

const char* FatigueTester::GetDeviceName() const noexcept
{
    return "Fatigue Tester";
}

void FatigueTester::RenderMainScreen() noexcept
{
    if (!display_ || !settings_) return;
    
    // Add small delay to ensure I2C bus is ready from previous operation
    vTaskDelay(pdMS_TO_TICKS(5));
    
    display_->clearDisplay();
    
    // Header (white background)
    display_->fillRect(0, 0, 128, 12, 1);
    display_->setTextColor(0);
    display_->setTextSize(1);
    display_->setCursor(2, 2);
    display_->print("Fatigue Tester");
    
    // Connection Status indicator (right side of header)
    TickType_t now_ticks = xTaskGetTickCount();
    bool connected = (last_status_tick_ > 0) && (now_ticks - last_status_tick_ < pdMS_TO_TICKS(5000));
    if (connected) {
        // Connected: solid black dot on white header
        display_->fillCircle(120, 6, 3, 0);
    } else {
        // Disconnected: hollow black circle with X
        display_->drawCircle(120, 6, 3, 0);
        display_->drawLine(118, 4, 122, 8, 0);
        display_->drawLine(122, 4, 118, 8, 0);
    }
    
    // Main Status Display (large, centered)
    display_->setTextColor(1);
    display_->setTextSize(2); // Large text for status
    
    const char* status_text = "";
    if (current_state_ == device_protocols::FatigueTestState::Running) {
        status_text = "RUNNING";
    } else if (current_state_ == device_protocols::FatigueTestState::Paused) {
        status_text = "PAUSED";
    } else if (current_state_ == device_protocols::FatigueTestState::Completed) {
        status_text = "DONE";
    } else if (current_state_ == device_protocols::FatigueTestState::Error) {
        status_text = "ERROR";
    } else {
        // Idle state - show connection/sync status
        if (connected && settings_synced_) {
            status_text = "READY";
        } else if (connected) {
            status_text = "SYNCING";
        } else {
            status_text = "OFFLINE";
        }
    }
    
    // Center the status text
    int16_t x1, y1;
    uint16_t w, h;
    display_->getTextBounds(status_text, 0, 0, &x1, &y1, &w, &h);
    display_->setCursor((128 - w) / 2, 25);
    display_->print(status_text);
    
    // If running or paused, show cycle progress
    if (current_state_ == device_protocols::FatigueTestState::Running || 
        current_state_ == device_protocols::FatigueTestState::Paused) {
        display_->setTextSize(1);
        display_->setCursor(0, 40);
        char buf[32];
        snprintf(buf, sizeof(buf), "Cycle: %lu / %lu", 
                 (unsigned long)current_cycle_, 
                 (unsigned long)settings_->fatigue_test.cycle_amount);
        display_->print(buf);
    } else if (current_state_ == device_protocols::FatigueTestState::Error) {
        // Show error code
        display_->setTextSize(1);
        display_->setCursor(0, 40);
        char buf[32];
        snprintf(buf, sizeof(buf), "Error: %d", error_code_);
        display_->print(buf);
    }
    
    // Navigation hints (small, at bottom)
    display_->setTextSize(1);
    display_->setCursor(0, 54);
    display_->print("ENC:Settings  OK:Test");
    
    display_->display();
}

void FatigueTester::RenderControlScreen() noexcept
{
    if (!display_ || !settings_) return;
    
    // Check for popup first
    if (popup_active_) {
        RenderPopup();
        return;
    }
    
    // Add small delay to ensure I2C bus is ready from previous operation
    vTaskDelay(pdMS_TO_TICKS(5));
    
    display_->clearDisplay();
    
    // Header
    if (current_state_ == device_protocols::FatigueTestState::Running) {
        display_->fillRect(0, 0, 128, 12, 1);
        display_->setTextColor(0);
        display_->setTextSize(1);
        display_->setCursor(40, 2);
        display_->print("RUNNING");
        
        // Data
        display_->setTextColor(1);
        display_->setTextSize(2);
        
        // Center the cycle count
        char buf[32];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)current_cycle_);
        int16_t x1, y1;
        uint16_t w, h;
        display_->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
        display_->setCursor((128 - w) / 2, 25);
        display_->print(buf);
        
        display_->setTextSize(1);
        display_->setCursor(20, 45);
        display_->print("Target: ");
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)settings_->fatigue_test.cycle_amount);
        display_->print(buf);
    } else if (current_state_ == device_protocols::FatigueTestState::Paused) {
        display_->drawRect(0, 0, 128, 12, 1);
        display_->setTextColor(1);
        display_->setTextSize(1);
        display_->setCursor(45, 2);
        display_->print("PAUSED");
        
        // Data
        display_->setTextSize(2);
        char buf[32];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)current_cycle_);
        int16_t x1, y1;
        uint16_t w, h;
        display_->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
        display_->setCursor((128 - w) / 2, 25);
        display_->print(buf);
        
        display_->setTextSize(1);
        display_->setCursor(20, 45);
        display_->print("Target: ");
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)settings_->fatigue_test.cycle_amount);
        display_->print(buf);
    } else if (current_state_ == device_protocols::FatigueTestState::Completed) {
        display_->clearDisplay();
        display_->setTextSize(1);
        display_->setTextColor(1);
        
        // Title
        display_->setCursor(20, 0);
        display_->print("Test Complete");
        
        // Final cycle count
        display_->setCursor(0, 20);
        display_->print("Total Cycles: ");
        char buf[32];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)current_cycle_);
        display_->print(buf);
    } else if (current_state_ == device_protocols::FatigueTestState::Error) {
        display_->clearDisplay();
        display_->setTextSize(1);
        display_->setTextColor(1);
        
        // Title
        display_->setCursor(30, 0);
        display_->print("ERROR");
        
        // Error code
        display_->setCursor(0, 20);
        display_->print("Code: ");
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", error_code_);
        display_->print(buf);
    } else {
        // Idle - show start prompt
        display_->clearDisplay();
        display_->setTextSize(1);
        display_->setTextColor(1);
        display_->setCursor(0, 0);
        display_->print("Control");
        display_->drawLine(0, 9, 128, 9, 1);
        display_->setCursor(0, 12);
        display_->print("Press CONFIRM");
        display_->setCursor(0, 24);
        display_->print("to start");
    }
    
    // Error footer (if errors exist and meet severity threshold)
    renderErrorFooter();
    
    display_->display();
}

void FatigueTester::renderErrorFooter() noexcept
{
    if (!display_ || !settings_ || error_count_ == 0) return;
    
    // Filter errors by severity threshold
    size_t displayable_count = 0;
    ErrorEntry displayable_errors[MAX_ERRORS_];
    
    for (size_t i = 0; i < error_count_; ++i) {
        if (errors_[i].severity >= settings_->fatigue_test.error_severity_min) {
            if (displayable_count < MAX_ERRORS_) {
                displayable_errors[displayable_count++] = errors_[i];
            }
        }
    }
    
    if (displayable_count == 0) return;
    
    // Draw line above footer
    display_->drawLine(0, 52, 128, 52, 1);
    
    // Display errors (up to 3, most significant first)
    int y_pos = 54;
    for (size_t i = 0; i < displayable_count && i < MAX_ERRORS_ && y_pos < 64; ++i) {
        char buf[32];
        snprintf(buf, sizeof(buf), "E%d", displayable_errors[i].code);
        display_->setCursor(0, y_pos);
        display_->print(buf);
        y_pos += 8;
    }
}

void FatigueTester::HandleButton(ButtonId button_id) noexcept
{
    // Handle popup first
    if (popup_active_) {
        if (button_id == ButtonId::Confirm) {
            if (popup_type_ == 1) { // Start confirmation
                espnow::SendCommand(GetDeviceId(), 1, nullptr, 0); // Start command
                current_state_ = device_protocols::FatigueTestState::Running;
            } else if (popup_type_ == 2) { // Stop confirmation
                espnow::SendCommand(GetDeviceId(), 4, nullptr, 0); // Stop command
                current_state_ = device_protocols::FatigueTestState::Idle;
            }
            popup_active_ = false;
        } else if (button_id == ButtonId::Back) {
            popup_active_ = false;
        }
        return;
    }
    
    // Handle menu navigation
    if (menu_active_) {
        if (button_id == ButtonId::Back) {
            if (editing_value_ || editing_choice_) {
                editing_value_ = false;
                editing_choice_ = false;
            } else {
                menu_active_ = false;
                // Save and send settings
                if (settings_) {
                    SettingsStore::Save(*settings_);
                    sendSettingsToDevice();
                }
            }
        } else if (button_id == ButtonId::Confirm) {
            if (editing_value_ || editing_choice_) {
                editing_value_ = false;
                editing_choice_ = false;
            } else {
                // Enter edit mode or execute action
                handleMenuEnter();
            }
        }
        return;
    }
    
    // Handle control screen buttons
    if (current_state_ == device_protocols::FatigueTestState::Idle) {
        if (button_id == ButtonId::Confirm) {
            // Show start confirmation popup
            popup_active_ = true;
            popup_type_ = 1; // Start
        }
    } else if (current_state_ == device_protocols::FatigueTestState::Running) {
        if (button_id == ButtonId::Confirm) {
            // Pause
            espnow::SendCommand(GetDeviceId(), 2, nullptr, 0); // Pause command
            current_state_ = device_protocols::FatigueTestState::Paused;
        } else if (button_id == ButtonId::Back) {
            // Show stop confirmation popup
            popup_active_ = true;
            popup_type_ = 2; // Stop
            popup_yes_selected_ = false; // Default to NO
        }
    } else if (current_state_ == device_protocols::FatigueTestState::Paused) {
        if (button_id == ButtonId::Confirm) {
            // Resume
            espnow::SendCommand(GetDeviceId(), 3, nullptr, 0); // Resume command
            current_state_ = device_protocols::FatigueTestState::Running;
        } else if (button_id == ButtonId::Back) {
            // Show stop confirmation popup
            popup_active_ = true;
            popup_type_ = 2; // Stop
            popup_yes_selected_ = false; // Default to NO
        }
    }
}

void FatigueTester::HandleEncoder(EC11Encoder::Direction direction) noexcept
{
    if (popup_active_) {
        // Toggle popup selection
        popup_yes_selected_ = !popup_yes_selected_;
        return;
    }
    
    if (menu_active_) {
        if (editing_value_) {
            // Adjust value
            if (direction == EC11Encoder::Direction::CW) {
                adjustCurrentValue(menu_edit_step_);
            } else {
                adjustCurrentValue(-static_cast<int32_t>(menu_edit_step_));
            }
        } else if (editing_choice_) {
            // Toggle choice
            toggleCurrentChoice();
        } else {
            // Navigate menu (normal: CW moves down, CCW moves up)
            if (direction == EC11Encoder::Direction::CW) {
                // CW moves down (increase index)
                menu_selected_index_++;
                if (menu_selected_index_ >= 7) menu_selected_index_ = 6;
            } else {
                // CCW moves up (decrease index)
                if (menu_selected_index_ > 0) menu_selected_index_--;
            }
        }
        return;
    }
}

void FatigueTester::HandleEncoderButton(bool pressed) noexcept
{
    if (!pressed) return;
    
    if (popup_active_) {
        // Confirm popup selection (encoder button = confirm)
        if (popup_yes_selected_) {
            if (popup_type_ == 1) { // Start
                espnow::SendCommand(GetDeviceId(), 1, nullptr, 0);
                current_state_ = device_protocols::FatigueTestState::Running;
            } else if (popup_type_ == 2) { // Stop
                espnow::SendCommand(GetDeviceId(), 4, nullptr, 0);
                current_state_ = device_protocols::FatigueTestState::Idle;
            }
        }
        popup_active_ = false;
        popup_yes_selected_ = false;
        return;
    }
    
    if (menu_active_) {
        if (editing_value_ || editing_choice_) {
            // Save and exit edit mode
            editing_value_ = false;
            editing_choice_ = false;
        } else {
            // Enter edit mode
            handleMenuEnter();
        }
        return;
    }
}

void FatigueTester::UpdateFromProtocol(const espnow::ProtoEvent& event) noexcept
{
    if (event.device_id != GetDeviceId()) return;
    
    if (event.type == espnow::MsgType::StatusUpdate && 
        event.payload_len >= sizeof(device_protocols::FatigueTestStatusPayload)) {
        device_protocols::FatigueTestStatusPayload status{};
        std::memcpy(&status, event.payload, sizeof(status));
        handleStatusUpdate(status);
    } else if (event.type == espnow::MsgType::ConfigResponse) {
        // Settings received from device
        if (event.payload_len >= sizeof(device_protocols::FatigueTestConfigPayload)) {
            device_protocols::FatigueTestConfigPayload config{};
            std::memcpy(&config, event.payload, sizeof(config));
            if (settings_) {
                settings_->fatigue_test.cycle_amount = config.cycle_amount;
                settings_->fatigue_test.time_per_cycle = config.time_per_cycle_sec;
                settings_->fatigue_test.dwell_time = config.dwell_time_sec;
                settings_->fatigue_test.bounds_method_stallguard = (config.bounds_method == 0);
                settings_synced_ = true;
                SettingsStore::Save(*settings_);
            }
        }
    } else if (event.type == espnow::MsgType::ConfigAck) {
        settings_synced_ = true;
    } else if (event.type == espnow::MsgType::TestComplete) {
        current_state_ = device_protocols::FatigueTestState::Completed;
    } else if (event.type == espnow::MsgType::Error) {
        current_state_ = device_protocols::FatigueTestState::Error;
        if (event.payload_len >= 1) {
            error_code_ = event.payload[0];
            // Default severity: assume high (3) if not specified
            uint8_t severity = (event.payload_len >= 2) ? event.payload[1] : 3;
            addError(error_code_, severity);
        }
    } else if (event.type == espnow::MsgType::ErrorClear) {
        // Test unit sent clear error command
        clearErrors();
    }
}

bool FatigueTester::IsConnected() const noexcept
{
    return DeviceBase::IsConnected();
}

void FatigueTester::RequestStatus() noexcept
{
    espnow::SendConfigRequest(GetDeviceId());
}

void FatigueTester::BuildSettingsMenu(class MenuBuilder& builder) noexcept
{
    if (!settings_) return;
    
    // Build menu structure with all settings items
    // Cycles: 1-100000, step 100
    builder.AddValueItem(nullptr, "Cycles", 
                        &settings_->fatigue_test.cycle_amount,
                        1, 100000, 100);
    
    // Time per cycle: 1-3600 seconds, step 1
    builder.AddValueItem(nullptr, "Time/Cycle",
                        &settings_->fatigue_test.time_per_cycle,
                        1, 3600, 1);
    
    // Dwell time: 0-60 seconds, step 1
    builder.AddValueItem(nullptr, "Dwell Time",
                        &settings_->fatigue_test.dwell_time,
                        0, 60, 1);
    
    // Bounds method: choice (StallGuard/Encoder)
    builder.AddChoiceItem(nullptr, "Bounds Mode",
                         &settings_->fatigue_test.bounds_method_stallguard);
    
    // Error severity minimum: 1-3, step 1
    builder.AddValueItem(nullptr, "Error Severity",
                        reinterpret_cast<uint32_t*>(&settings_->fatigue_test.error_severity_min),
                        1, 3, 1);
    
    // Orientation: choice (Normal/Flipped)
    builder.AddChoiceItem(nullptr, "Flip Screen",
                         &settings_->ui.orientation_flipped);
}

void FatigueTester::renderStatusScreen() noexcept
{
    RenderMainScreen();
}

void FatigueTester::handleStatusUpdate(const device_protocols::FatigueTestStatusPayload& status) noexcept
{
    current_cycle_ = status.cycle_number;
    current_state_ = static_cast<device_protocols::FatigueTestState>(status.state);
    error_code_ = status.err_code;
    last_status_tick_ = xTaskGetTickCount();
    connected_ = true;
}

void FatigueTester::sendSettingsToDevice() noexcept
{
    if (!settings_) return;
    
    device_protocols::FatigueTestConfigPayload config{};
    config.cycle_amount = settings_->fatigue_test.cycle_amount;
    config.time_per_cycle_sec = settings_->fatigue_test.time_per_cycle;
    config.dwell_time_sec = settings_->fatigue_test.dwell_time;
    config.bounds_method = settings_->fatigue_test.bounds_method_stallguard ? 0 : 1;
    
    espnow::SendConfigSet(GetDeviceId(), &config, sizeof(config));
    settings_synced_ = false;
}

void FatigueTester::adjustCurrentValue(int32_t delta) noexcept
{
    if (!settings_) return;
    
    switch (menu_selected_index_) {
        case 0: { // Cycles
            int32_t new_val = static_cast<int32_t>(settings_->fatigue_test.cycle_amount) + delta;
            if (new_val < 1) new_val = 1;
            if (new_val > 100000) new_val = 100000;
            settings_->fatigue_test.cycle_amount = static_cast<uint32_t>(new_val);
            break;
        }
        case 1: { // Time per cycle
            int32_t new_val = static_cast<int32_t>(settings_->fatigue_test.time_per_cycle) + delta;
            if (new_val < 1) new_val = 1;
            if (new_val > 3600) new_val = 3600;
            settings_->fatigue_test.time_per_cycle = static_cast<uint32_t>(new_val);
            break;
        }
        case 2: { // Dwell time
            int32_t new_val = static_cast<int32_t>(settings_->fatigue_test.dwell_time) + delta;
            if (new_val < 0) new_val = 0;
            if (new_val > 60) new_val = 60;
            settings_->fatigue_test.dwell_time = static_cast<uint32_t>(new_val);
            break;
        }
        case 4: { // Error severity
            int32_t new_val = static_cast<int32_t>(settings_->fatigue_test.error_severity_min) + delta;
            if (new_val < 1) new_val = 1;
            if (new_val > 3) new_val = 3;
            settings_->fatigue_test.error_severity_min = static_cast<uint8_t>(new_val);
            break;
        }
    }
}

void FatigueTester::toggleCurrentChoice() noexcept
{
    if (!settings_) return;
    
    if (menu_selected_index_ == 3) { // Bounds Mode
        settings_->fatigue_test.bounds_method_stallguard = !settings_->fatigue_test.bounds_method_stallguard;
    } else if (menu_selected_index_ == 5) { // Flip Screen
        settings_->ui.orientation_flipped = !settings_->ui.orientation_flipped;
        if (display_) {
            display_->setRotation(settings_->ui.orientation_flipped ? 2 : 0);
            // Force clear to prevent artifacts
            display_->clearDisplay();
            display_->display();
        }
    }
}

void FatigueTester::handleMenuEnter() noexcept
{
    if (menu_selected_index_ == 6) { // Back
        menu_active_ = false;
        if (settings_) {
            SettingsStore::Save(*settings_);
            sendSettingsToDevice();
        }
    } else if (menu_selected_index_ < 3 || menu_selected_index_ == 4) { // Value items
        editing_value_ = true;
        // Set edit parameters based on item
        switch (menu_selected_index_) {
            case 0: menu_edit_step_ = 100; break; // Cycles
            case 1: menu_edit_step_ = 1; break;   // Time per cycle
            case 2: menu_edit_step_ = 1; break;   // Dwell time
            case 4: menu_edit_step_ = 1; break;   // Error severity
        }
    } else if (menu_selected_index_ == 3 || menu_selected_index_ == 5) { // Choice items
        editing_choice_ = true;
    }
}

void FatigueTester::RenderSettingsMenu() noexcept
{
    if (!display_ || !settings_) return;
    
    // Add small delay to ensure I2C bus is ready from previous operation
    vTaskDelay(pdMS_TO_TICKS(5));
    
    display_->clearDisplay();
    
    // Draw title
    display_->setTextSize(1);
    display_->setTextColor(1);
    display_->setCursor(0, 0);
    display_->print("Settings");
    display_->drawLine(0, 9, 128, 9, 1);
    
    if (editing_value_) {
        // Value edit screen
        const char* labels[] = {"Cycles", "Time/Cycle", "Dwell Time", "", "Error Severity"};
        const char* units[] = {"", "s", "s", "", ""};
        if (menu_selected_index_ < 3 || menu_selected_index_ == 4) {
            display_->setCursor(0, 20);
            display_->print(labels[menu_selected_index_]);
            display_->drawLine(0, 29, 128, 29, 1);
            
            // Large value display
            display_->setTextSize(2);
            char buf[32];
            uint32_t val = 0;
            switch (menu_selected_index_) {
                case 0: val = settings_->fatigue_test.cycle_amount; break;
                case 1: val = settings_->fatigue_test.time_per_cycle; break;
                case 2: val = settings_->fatigue_test.dwell_time; break;
                case 4: val = settings_->fatigue_test.error_severity_min; break;
            }
            snprintf(buf, sizeof(buf), "%lu%s", (unsigned long)val, units[menu_selected_index_]);
            int16_t x1, y1;
            uint16_t w, h;
            display_->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
            display_->setCursor((128 - w) / 2, 35);
            display_->print(buf);
            
            display_->setTextSize(1);
            display_->setCursor(0, 55);
            display_->print("Rotate: Adjust  OK: Save");
        }
    } else if (editing_choice_) {
        // Choice edit screen
        const char* label = "";
        bool* val_ptr = nullptr;
        const char* opt1 = "";
        const char* opt2 = "";
        
        if (menu_selected_index_ == 3) { // Bounds Mode
            label = "Bounds Mode";
            val_ptr = &settings_->fatigue_test.bounds_method_stallguard;
            opt1 = "[ENC]";
            opt2 = "[STALL]";
        } else if (menu_selected_index_ == 5) { // Flip Screen
            label = "Flip Screen";
            val_ptr = &settings_->ui.orientation_flipped;
            opt1 = "[NORM]";
            opt2 = "[FLIP]";
        }
        
        display_->setCursor(0, 20);
        display_->print(label);
        
        bool val = val_ptr ? *val_ptr : false;
        
        // Option 1 (False)
        if (!val) {
            display_->fillRect(10, 35, 50, 12, 1);
            display_->setTextColor(0);
        } else {
            display_->setTextColor(1);
        }
        display_->setCursor(12, 37);
        display_->print(opt1);
        
        // Option 2 (True)
        if (val) {
            display_->fillRect(68, 35, 50, 12, 1);
            display_->setTextColor(0);
        } else {
            display_->setTextColor(1);
        }
        display_->setCursor(70, 37);
        display_->print(opt2);
        
        display_->setTextColor(1);
        display_->setCursor(0, 55);
        display_->print("Rotate: Sel  Push: OK");
    } else {
        // Menu list
        const char* menu_items[] = {
            "Cycles",
            "Time/Cycle",
            "Dwell Time",
            "Bounds Mode",
            "Error Severity",
            "Flip Screen",
            "Back"
        };
        
        int start_idx = (menu_selected_index_ > 3) ? menu_selected_index_ - 3 : 0;
        int end_idx = start_idx + 4;
        if (end_idx > 7) {
            end_idx = 7;
            start_idx = 3;
        }
        
        int y = 12;
        for (int i = start_idx; i < end_idx; i++) {
            bool selected = (i == menu_selected_index_);
            
            if (selected) {
                display_->fillRect(0, y - 1, 128, 11, 1);
                display_->setTextColor(0);
            } else {
                display_->setTextColor(1);
            }
            
            display_->setCursor(2, y);
            display_->print(menu_items[i]);
            
            // Display value if applicable
            if (i == 0) { // Cycles
                char buf[16];
                snprintf(buf, sizeof(buf), "[%lu]", (unsigned long)settings_->fatigue_test.cycle_amount);
                int16_t x1, y1;
                uint16_t w, h;
                display_->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
                display_->setCursor(126 - w, y);
                display_->print(buf);
            } else if (i == 1) { // Time/Cycle
                char buf[16];
                snprintf(buf, sizeof(buf), "[%lus]", (unsigned long)settings_->fatigue_test.time_per_cycle);
                int16_t x1, y1;
                uint16_t w, h;
                display_->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
                display_->setCursor(126 - w, y);
                display_->print(buf);
            } else if (i == 2) { // Dwell Time
                char buf[16];
                snprintf(buf, sizeof(buf), "[%lus]", (unsigned long)settings_->fatigue_test.dwell_time);
                int16_t x1, y1;
                uint16_t w, h;
                display_->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
                display_->setCursor(126 - w, y);
                display_->print(buf);
            } else if (i == 3) { // Bounds Mode
                const char* str = settings_->fatigue_test.bounds_method_stallguard ? "[STALL]" : "[ENC]";
                int16_t x1, y1;
                uint16_t w, h;
                display_->getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
                display_->setCursor(126 - w, y);
                display_->print(str);
            } else if (i == 4) { // Error Severity
                char buf[16];
                snprintf(buf, sizeof(buf), "[%d]", settings_->fatigue_test.error_severity_min);
                int16_t x1, y1;
                uint16_t w, h;
                display_->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
                display_->setCursor(126 - w, y);
                display_->print(buf);
            } else if (i == 5) { // Flip Screen
                const char* str = settings_->ui.orientation_flipped ? "[FLIP]" : "[NORM]";
                int16_t x1, y1;
                uint16_t w, h;
                display_->getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
                display_->setCursor(126 - w, y);
                display_->print(str);
            }
            
            y += 12;
        }
    }
    
    display_->display();
}

void FatigueTester::RenderPopup() noexcept
{
    if (!display_ || !popup_active_) return;
    
    display_->clearDisplay();
    
    // Draw border
    display_->drawRect(0, 0, 128, 64, 1);
    display_->drawRect(2, 2, 124, 60, 1);
    
    // Title
    display_->setTextSize(1);
    display_->setTextColor(1);
    display_->setCursor(10, 8);
    display_->print("CONFIRMATION");
    display_->drawLine(10, 18, 118, 18, 1);
    
    // Message
    const char* msg = (popup_type_ == 1) ? "Start Test?" : "Stop Test?";
    int len = strlen(msg);
    int x = (128 - (len * 6)) / 2;
    if (x < 4) x = 4;
    display_->setCursor(x, 25);
    display_->print(msg);
    
    // Buttons
    // NO (Back)
    if (!popup_yes_selected_) {
        display_->fillRect(8, 43, 55, 12, 1);
        display_->setTextColor(0);
    } else {
        display_->setTextColor(1);
    }
    display_->setCursor(10, 45);
    display_->print("NO (Back)");
    
    // YES (Ok)
    if (popup_yes_selected_) {
        display_->fillRect(68, 43, 55, 12, 1);
        display_->setTextColor(0);
    } else {
        display_->setTextColor(1);
    }
    display_->setCursor(70, 45);
    display_->print("YES (Ok)");
    
    display_->display();
}

void FatigueTester::addError(uint8_t code, uint8_t severity) noexcept
{
    // Remove existing error with same code
    for (size_t i = 0; i < error_count_; ++i) {
        if (errors_[i].code == code) {
            // Update severity and timestamp
            errors_[i].severity = severity;
            errors_[i].timestamp = xTaskGetTickCount();
            return;
        }
    }
    
    // Add new error if space available
    if (error_count_ < MAX_ERRORS_) {
        errors_[error_count_].code = code;
        errors_[error_count_].severity = severity;
        errors_[error_count_].timestamp = xTaskGetTickCount();
        error_count_++;
    } else {
        // Replace oldest error
        size_t oldest_idx = 0;
        TickType_t oldest_time = errors_[0].timestamp;
        for (size_t i = 1; i < MAX_ERRORS_; ++i) {
            if (errors_[i].timestamp < oldest_time) {
                oldest_time = errors_[i].timestamp;
                oldest_idx = i;
            }
        }
        errors_[oldest_idx].code = code;
        errors_[oldest_idx].severity = severity;
        errors_[oldest_idx].timestamp = xTaskGetTickCount();
    }
}

void FatigueTester::clearErrors() noexcept
{
    error_count_ = 0;
    for (size_t i = 0; i < MAX_ERRORS_; ++i) {
        errors_[i] = {0, 0, 0};
    }
}

void FatigueTester::checkConfirmHold(ButtonId button_id) noexcept
{
    TickType_t now = xTaskGetTickCount();
    const TickType_t hold_duration = pdMS_TO_TICKS(5000); // 5 seconds
    
    if (button_id == ButtonId::Confirm) {
        if (!confirm_held_) {
            // Start holding
            confirm_hold_start_ = now;
            confirm_held_ = true;
        } else {
            // Check if held for 5 seconds
            if (now - confirm_hold_start_ >= hold_duration) {
                clearErrors();
                confirm_held_ = false;
            }
        }
    } else {
        // Any other button release cancels hold
        confirm_held_ = false;
    }
}
