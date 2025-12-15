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
#include <cstdarg>
#include <cstdio>
#include <cstring>

static const char* TAG_ = "FatigueTester";

FatigueTester::FatigueTester(Adafruit_SH1106* display, Settings* settings) noexcept
    : DeviceBase(display, settings)
    , current_state_(device_protocols::FatigueTestState::Idle)
    , current_cycle_(0)
    , error_code_(0)
    , popup_active_(false)
    , popup_mode_(PopupMode::None)
    , popup_selected_index_(0)
    , settings_synced_(false)
    , pending_command_id_(0)
    , pending_command_tick_(0)
    , menu_active_(false)
    , menu_selected_index_(0)
    , editing_value_(false)
    , editing_choice_(false)
    , menu_edit_step_(1)
    , error_count_(0)
    , confirm_hold_start_(0)
    , confirm_held_(false)
    , log_lines_{}
    , log_head_(0)
    , last_logged_state_(device_protocols::FatigueTestState::Idle)
    , last_logged_cycle_(0)
    , last_logged_error_code_(0)
    , not_connected_flash_until_tick_(0)
{
    // Initialize error array
    for (size_t i = 0; i < MAX_ERRORS_; ++i) {
        errors_[i] = {0, 0, 0};
    }

    // Init log buffer
    for (uint8_t i = 0; i < LOG_LINES_; ++i) {
        log_lines_[i][0] = '\0';
    }
    pushLogLine("Boot: requesting...");
    
    // Request initial config from device
    RequestStatus();
}

void FatigueTester::pushLogLine(const char* fmt, ...) noexcept
{
    if (!fmt) return;

    char buf[LOG_LINE_CHARS_]{};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    // Write into ring buffer
    std::snprintf(log_lines_[log_head_], LOG_LINE_CHARS_, "%s", buf);
    log_head_ = static_cast<uint8_t>((log_head_ + 1) % LOG_LINES_);
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
    
    // Body: state summary + small "log window" in the area that used to show big OFFLINE text.
    display_->setTextColor(1);
    display_->setTextSize(1);

    const char* state_str = "IDLE";
    switch (current_state_) {
        case device_protocols::FatigueTestState::Running:   state_str = "RUN"; break;
        case device_protocols::FatigueTestState::Paused:    state_str = "PAUSE"; break;
        case device_protocols::FatigueTestState::Completed: state_str = "DONE"; break;
        case device_protocols::FatigueTestState::Error:     state_str = "ERR"; break;
        default: state_str = "IDLE"; break;
    }

    // Top row (y=14): state + cycle summary (or offline/sync)
    display_->setCursor(0, 14);
    if (!connected) {
        display_->print("OFFLINE");
    } else if (!settings_synced_) {
        display_->print("SYNCING");
    } else {
        char top[32];
        snprintf(top, sizeof(top), "%s %lu/%lu",
                 state_str,
                 (unsigned long)current_cycle_,
                 (unsigned long)settings_->fatigue_test.cycle_amount);
        display_->print(top);
    }

    // Log window frame (y=24..51)
    display_->drawRect(0, 24, 128, 28, 1);

    // Render last LOG_LINES_ messages, newest at bottom
    // Line Y positions: 26, 34, 42 (text size 1 => 8px height)
    for (uint8_t i = 0; i < LOG_LINES_; ++i) {
        uint8_t idx = static_cast<uint8_t>((log_head_ + LOG_LINES_ - 1 - i) % LOG_LINES_);
        const char* line = log_lines_[idx];
        if (!line || line[0] == '\0') continue;
        int y = 42 - (i * 8);
        display_->setCursor(2, y);
        display_->print(line);
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

    // Unified, strict 128x64 layout:
    // - Header: y=0..11
    // - Body:   y=12..51
    // - Footer: y=52..63

    TickType_t now_ticks = xTaskGetTickCount();
    bool connected = (last_status_tick_ > 0) && (now_ticks - last_status_tick_ < pdMS_TO_TICKS(5000));
    bool cmd_pending = (pending_command_id_ != 0) && (now_ticks - pending_command_tick_ < pdMS_TO_TICKS(2000));
    if (!cmd_pending && pending_command_id_ != 0) {
        pending_command_id_ = 0;
        pending_command_tick_ = 0;
    }

    // Header bar (inverted)
    display_->fillRect(0, 0, 128, 12, 1);
    display_->setTextColor(0);
    display_->setTextSize(1);
    display_->setCursor(2, 2);
    display_->print("TEST");

    // State tag (right side)
    const char* state_tag = "IDLE";
    switch (current_state_) {
        case device_protocols::FatigueTestState::Running: state_tag = "RUN"; break;
        case device_protocols::FatigueTestState::Paused: state_tag = "PAUSE"; break;
        case device_protocols::FatigueTestState::Completed: state_tag = "DONE"; break;
        case device_protocols::FatigueTestState::Error: state_tag = "ERR"; break;
        default: state_tag = "IDLE"; break;
    }
    display_->setCursor(86, 2);
    display_->print(state_tag);
    if (cmd_pending) {
        display_->setCursor(110, 2);
        display_->print("...");
    }

    // Connection dot (far right)
    if (connected) {
        display_->fillCircle(124, 6, 2, 0);
    } else {
        display_->drawCircle(124, 6, 2, 0);
    }

    // Body
    display_->setTextColor(1);

    // Small status line (y=14) with optional "flash" feedback when user tries actions while offline
    display_->setTextSize(1);
    const bool flash_nc = (!connected && not_connected_flash_until_tick_ != 0 && now_ticks < not_connected_flash_until_tick_);
    if (flash_nc) {
        // Invert the line to make it obvious
        display_->fillRect(0, 12, 128, 10, 1);
        display_->setTextColor(0);
    } else {
        display_->setTextColor(1);
    }
    display_->setCursor(0, 14);
    if (!connected) {
        display_->print("NOT CONNECTED");
    } else if (!settings_synced_) {
        display_->print("SYNCING...");
    } else if (cmd_pending) {
        display_->print("SENDING...");
    } else {
        display_->print("READY");
    }
    display_->setTextColor(1);

    // Big cycle count (y=22)
    display_->setTextSize(2);
    char cycle_buf[16];
    if (!connected) {
        std::snprintf(cycle_buf, sizeof(cycle_buf), "--");
    } else {
        std::snprintf(cycle_buf, sizeof(cycle_buf), "%lu", (unsigned long)current_cycle_);
    }
    int16_t x1, y1;
    uint16_t w, h;
    display_->getTextBounds(cycle_buf, 0, 0, &x1, &y1, &w, &h);
    display_->setCursor((128 - w) / 2, 22);
    display_->print(cycle_buf);

    // Target row (y=40) - only setting we show on this screen
    display_->setTextSize(1);
    display_->setCursor(0, 40);
    display_->print("Target ");
    display_->print((unsigned long)settings_->fatigue_test.cycle_amount);

    // Footer bar (inverted)
    display_->fillRect(0, 52, 128, 12, 1);
    display_->setTextColor(0);
    display_->setTextSize(1);
    display_->setCursor(2, 54);
    display_->print("OK:Actions  BACK:Exit");

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
        if (button_id == ButtonId::Back) {
            popup_active_ = false;
            popup_mode_ = PopupMode::None;
            popup_selected_index_ = 0;
            return;
        }

        if (button_id == ButtonId::Confirm) {
            // Execute based on popup mode + selection
            if (popup_mode_ == PopupMode::StartConfirm) {
                // 0=BACK, 1=START
                if (popup_selected_index_ == 1) {
                    espnow::SendCommand(GetDeviceId(), 1, nullptr, 0); // START
                    pending_command_id_ = 1;
                    pending_command_tick_ = xTaskGetTickCount();
                }
            } else if (popup_mode_ == PopupMode::RunningActions) {
                // 0=BACK, 1=PAUSE, 2=STOP
                if (popup_selected_index_ == 1) {
                    espnow::SendCommand(GetDeviceId(), 2, nullptr, 0); // PAUSE
                    pending_command_id_ = 2;
                    pending_command_tick_ = xTaskGetTickCount();
                } else if (popup_selected_index_ == 2) {
                    espnow::SendCommand(GetDeviceId(), 4, nullptr, 0); // STOP
                    pending_command_id_ = 4;
                    pending_command_tick_ = xTaskGetTickCount();
                }
            } else if (popup_mode_ == PopupMode::PausedActions) {
                // 0=BACK, 1=RESUME, 2=STOP
                if (popup_selected_index_ == 1) {
                    espnow::SendCommand(GetDeviceId(), 3, nullptr, 0); // RESUME
                    pending_command_id_ = 3;
                    pending_command_tick_ = xTaskGetTickCount();
                } else if (popup_selected_index_ == 2) {
                    espnow::SendCommand(GetDeviceId(), 4, nullptr, 0); // STOP
                    pending_command_id_ = 4;
                    pending_command_tick_ = xTaskGetTickCount();
                }
            }

            popup_active_ = false;
            popup_mode_ = PopupMode::None;
            popup_selected_index_ = 0;
            return;
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
            // If connected, ask user to confirm starting (popup).
            TickType_t now_ticks = xTaskGetTickCount();
            bool connected = (last_status_tick_ > 0) && (now_ticks - last_status_tick_ < pdMS_TO_TICKS(5000));
            if (connected) {
                popup_active_ = true;
                popup_mode_ = PopupMode::StartConfirm;
                popup_selected_index_ = 1; // default to START
            } else {
                // Brief visual feedback on the control screen.
                not_connected_flash_until_tick_ = now_ticks + pdMS_TO_TICKS(1000);
                RequestStatus();
            }
        }
    } else if (current_state_ == device_protocols::FatigueTestState::Running) {
        if (button_id == ButtonId::Confirm) {
            // Show action popup: BACK / PAUSE / STOP
            popup_active_ = true;
            popup_mode_ = PopupMode::RunningActions;
            popup_selected_index_ = 1; // default to PAUSE
        }
    } else if (current_state_ == device_protocols::FatigueTestState::Paused) {
        if (button_id == ButtonId::Confirm) {
            // Show action popup: BACK / RESUME / STOP
            popup_active_ = true;
            popup_mode_ = PopupMode::PausedActions;
            popup_selected_index_ = 1; // default to RESUME
        }
    }
}

void FatigueTester::HandleEncoder(EC11Encoder::Direction direction) noexcept
{
    if (popup_active_) {
        // Navigate popup selection
        int max_idx = 0;
        if (popup_mode_ == PopupMode::StartConfirm) {
            max_idx = 1; // 0..1
        } else if (popup_mode_ == PopupMode::RunningActions || popup_mode_ == PopupMode::PausedActions) {
            max_idx = 2; // 0..2
        }

        if (direction == EC11Encoder::Direction::CW) {
            if (popup_selected_index_ >= (uint8_t)max_idx) popup_selected_index_ = 0;
            else popup_selected_index_++;
        } else if (direction == EC11Encoder::Direction::CCW) {
            if (popup_selected_index_ == 0) popup_selected_index_ = (uint8_t)max_idx;
            else popup_selected_index_--;
        }
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
        // Encoder button behaves like CONFIRM in popup
        HandleButton(ButtonId::Confirm);
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

    // Encoder button on control screen behaves like CONFIRM (opens the same popups).
    HandleButton(ButtonId::Confirm);
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
                last_status_tick_ = xTaskGetTickCount();
                connected_ = true;
                SettingsStore::Save(*settings_);
                pushLogLine("CFG rx");
            }
        }
    } else if (event.type == espnow::MsgType::ConfigAck) {
        settings_synced_ = true;
        last_status_tick_ = xTaskGetTickCount();
        connected_ = true;
        pushLogLine("CFG ack");
    } else if (event.type == espnow::MsgType::CommandAck) {
        pending_command_id_ = 0;
        pending_command_tick_ = 0;
        last_status_tick_ = xTaskGetTickCount();
        connected_ = true;
        pushLogLine("CMD ack");
    } else if (event.type == espnow::MsgType::TestComplete) {
        current_state_ = device_protocols::FatigueTestState::Completed;
        pushLogLine("DONE");
    } else if (event.type == espnow::MsgType::Error) {
        current_state_ = device_protocols::FatigueTestState::Error;
        if (event.payload_len >= 1) {
            error_code_ = event.payload[0];
            // Default severity: assume high (3) if not specified
            uint8_t severity = (event.payload_len >= 2) ? event.payload[1] : 3;
            addError(error_code_, severity);
            pushLogLine("ERR E%u S%u", (unsigned)error_code_, (unsigned)severity);
        }
    } else if (event.type == espnow::MsgType::ErrorClear) {
        // Test unit sent clear error command
        clearErrors();
        pushLogLine("Errors cleared");
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
    device_protocols::FatigueTestState prev_state = current_state_;
    uint8_t prev_err = error_code_;
    uint32_t prev_cycle = current_cycle_;

    current_cycle_ = status.cycle_number;
    current_state_ = static_cast<device_protocols::FatigueTestState>(status.state);
    error_code_ = status.err_code;
    last_status_tick_ = xTaskGetTickCount();
    connected_ = true;
    pending_command_id_ = 0;
    pending_command_tick_ = 0;

    // Log only meaningful changes (avoid spamming the small log window).
    if (current_state_ != prev_state) {
        const char* s = "IDLE";
        switch (current_state_) {
            case device_protocols::FatigueTestState::Running: s = "RUN"; break;
            case device_protocols::FatigueTestState::Paused: s = "PAUSE"; break;
            case device_protocols::FatigueTestState::Completed: s = "DONE"; break;
            case device_protocols::FatigueTestState::Error: s = "ERR"; break;
            default: s = "IDLE"; break;
        }
        pushLogLine("State %s", s);
        last_logged_state_ = current_state_;
    }

    if (error_code_ != prev_err && error_code_ != 0) {
        pushLogLine("Err E%u", (unsigned)error_code_);
        last_logged_error_code_ = error_code_;
    }

    // Light touch progress logging: every 100 cycles while running.
    if (current_state_ == device_protocols::FatigueTestState::Running &&
        current_cycle_ != prev_cycle &&
        (current_cycle_ % 100U) == 0U) {
        pushLogLine("Cycle %lu", (unsigned long)current_cycle_);
        last_logged_cycle_ = current_cycle_;
    }
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
    const char* msg = "";
    if (popup_mode_ == PopupMode::StartConfirm) msg = "Start Test?";
    else if (popup_mode_ == PopupMode::RunningActions) msg = "Test Running";
    else if (popup_mode_ == PopupMode::PausedActions) msg = "Test Paused";
    else msg = "Action";
    int len = strlen(msg);
    int x = (128 - (len * 6)) / 2;
    if (x < 4) x = 4;
    display_->setCursor(x, 25);
    display_->print(msg);
    
    // Options row
    display_->setTextSize(1);
    const int y = 43;

    auto draw_option = [&](int idx, int x, int w, const char* label) {
        bool selected = (popup_selected_index_ == (uint8_t)idx);
        if (selected) {
            display_->fillRect(x, y, w, 12, 1);
            display_->setTextColor(0);
        } else {
            display_->setTextColor(1);
        }
        // crude centering: 6px per char
        int label_len = (int)strlen(label);
        int tx = x + (w - (label_len * 6)) / 2;
        if (tx < x + 1) tx = x + 1;
        display_->setCursor(tx, y + 2);
        display_->print(label);
    };

    if (popup_mode_ == PopupMode::StartConfirm) {
        // BACK / START
        draw_option(0, 6, 55, "BACK");
        draw_option(1, 67, 55, "START");
    } else if (popup_mode_ == PopupMode::RunningActions) {
        // BACK / PAUSE / STOP
        draw_option(0, 4, 38, "BACK");
        draw_option(1, 45, 38, "PAUSE");
        draw_option(2, 86, 38, "STOP");
    } else if (popup_mode_ == PopupMode::PausedActions) {
        // BACK / RESUME / STOP
        draw_option(0, 4, 38, "BACK");
        draw_option(1, 45, 38, "RESUME");
        draw_option(2, 86, 38, "STOP");
    } else {
        draw_option(0, 6, 116, "BACK");
    }
    
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
