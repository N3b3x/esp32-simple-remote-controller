/**
 * @file fatigue_tester.cpp
 * @brief Fatigue test device implementation with full menu and screen support
 * 
 * Menu structure (12 items) - PROTOCOL V2: velocity/acceleration control
 *   0: Cycles          (uint32, step 100)
 *   1: VMAX            (float RPM, step 5.0) - Max oscillation velocity
 *   2: AMAX            (float rev/s², step 0.5) - Oscillation acceleration
 *   3: Dwell Time      (uint32 ms, step 100)
 *   4: Bounds Mode     (choice: StallGuard/Encoder)
 *   5: Search Speed    (float RPM, step 5.0)
 *   6: SG Min Vel      (float RPM, step 1.0)
 *   7: Current Factor  (float 0.0-1.0, step 0.05)
 *   8: Search Accel    (float rev/s², step 0.5)
 *   9: Error Severity  (uint8 1-3, step 1)
 *  10: Flip Screen     (choice: Normal/Flipped)
 *  11: Back            (saves and exits)
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
    , editing_float_(false)
    , editing_choice_(false)
    , menu_edit_step_(1)
    , menu_edit_step_float_(1.0f)
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
            if (editing_value_ || editing_float_ || editing_choice_) {
                editing_value_ = false;
                editing_float_ = false;
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
            if (editing_value_ || editing_float_ || editing_choice_) {
                editing_value_ = false;
                editing_float_ = false;
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
            // Adjust integer value
            if (direction == EC11Encoder::Direction::CW) {
                adjustCurrentValue(menu_edit_step_);
            } else {
                adjustCurrentValue(-static_cast<int32_t>(menu_edit_step_));
            }
        } else if (editing_float_) {
            // Adjust float value
            if (direction == EC11Encoder::Direction::CW) {
                adjustCurrentFloatValue(1);
            } else {
                adjustCurrentFloatValue(-1);
            }
        } else if (editing_choice_) {
            // Toggle choice
            toggleCurrentChoice();
        } else {
            // Navigate menu (normal: CW moves down, CCW moves up)
            if (direction == EC11Encoder::Direction::CW) {
                // CW moves down (increase index)
                menu_selected_index_++;
                if (menu_selected_index_ >= MENU_ITEM_COUNT) menu_selected_index_ = MENU_ITEM_COUNT - 1;
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
        if (editing_value_ || editing_float_ || editing_choice_) {
            // Save and exit edit mode
            editing_value_ = false;
            editing_float_ = false;
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
        // Settings received from device - handle both base and extended fields
        // PROTOCOL V2 layout: 17 base bytes, 16 extended bytes, 1 SGT byte
        constexpr size_t BASE_CONFIG_SIZE = 17; // cycle_amount(4) + vmax(4) + amax(4) + dwell_ms(4) + bounds_method(1)
        constexpr size_t EXT_CONFIG_V1_SIZE = 17 + (4 * 4); // 33 bytes (4 floats for bounds finding)
        constexpr size_t EXT_CONFIG_V2_SIZE = EXT_CONFIG_V1_SIZE + 1; // + SGT
        
        if (event.payload_len >= BASE_CONFIG_SIZE) {
            device_protocols::FatigueTestConfigPayload config{};
            // Copy only the received bytes to handle older firmware without extended fields
            size_t copy_len = (event.payload_len < sizeof(config)) ? event.payload_len : sizeof(config);
            std::memcpy(&config, event.payload, copy_len);
            
            // DON'T overwrite settings while user is editing in the menu
            // Only update connection status and sync flag
            if (menu_active_) {
                last_status_tick_ = xTaskGetTickCount();
                connected_ = true;
                pushLogLine("CFG rx (menu)");
                // Don't update settings_ or save - user is editing
            } else if (settings_) {
                // Base fields (always present) - direct velocity/acceleration control
                settings_->fatigue_test.cycle_amount = config.cycle_amount;
                settings_->fatigue_test.oscillation_vmax_rpm = config.oscillation_vmax_rpm;
                settings_->fatigue_test.oscillation_amax_rev_s2 = config.oscillation_amax_rev_s2;
                settings_->fatigue_test.dwell_time_ms = config.dwell_time_ms;
                settings_->fatigue_test.bounds_method_stallguard = (config.bounds_method == 0);
                
                // Extended fields (v1): floats for bounds finding config
                if (event.payload_len >= EXT_CONFIG_V1_SIZE) {
                    settings_->fatigue_test.bounds_search_velocity_rpm = config.bounds_search_velocity_rpm;
                    settings_->fatigue_test.stallguard_min_velocity_rpm = config.stallguard_min_velocity_rpm;
                    settings_->fatigue_test.stall_detection_current_factor = config.stall_detection_current_factor;
                    settings_->fatigue_test.bounds_search_accel_rev_s2 = config.bounds_search_accel_rev_s2;

                    // Extended v2: SGT
                    settings_->fatigue_test.stallguard_sgt = 127;
                    if (event.payload_len >= EXT_CONFIG_V2_SIZE) {
                        settings_->fatigue_test.stallguard_sgt = config.stallguard_sgt;
                    }

                    ESP_LOGI(TAG_, "Config: VMAX=%.1f RPM, AMAX=%.1f rev/s², dwell=%lu ms, bounds_vel=%.1f, SGT=%d",
                             config.oscillation_vmax_rpm, config.oscillation_amax_rev_s2,
                             (unsigned long)config.dwell_time_ms,
                             config.bounds_search_velocity_rpm,
                             static_cast<int>(settings_->fatigue_test.stallguard_sgt));
                } else {
                    ESP_LOGI(TAG_, "Config received (base fields only, payload_len=%u)", event.payload_len);
                }
                
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
    
    // Max Velocity (VMAX): 5-120 RPM, step 5
    builder.AddFloatItem(nullptr, "VMAX (RPM)",
                        &settings_->fatigue_test.oscillation_vmax_rpm,
                        5.0f, 120.0f, 5.0f);
    
    // Acceleration (AMAX): 0.5-30 rev/s², step 0.5
    builder.AddFloatItem(nullptr, "AMAX (r/s²)",
                        &settings_->fatigue_test.oscillation_amax_rev_s2,
                        0.5f, 30.0f, 0.5f);
    
    // Dwell time: 0-10000 ms, step 100
    builder.AddValueItem(nullptr, "Dwell (ms)",
                        &settings_->fatigue_test.dwell_time_ms,
                        0, 10000, 100);
    
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
    
    // Build config payload with all fields including extended ones
    device_protocols::FatigueTestConfigPayload config{};
    
    // Base fields - PROTOCOL V2: direct velocity/acceleration control
    config.cycle_amount = settings_->fatigue_test.cycle_amount;
    config.oscillation_vmax_rpm = settings_->fatigue_test.oscillation_vmax_rpm;
    config.oscillation_amax_rev_s2 = settings_->fatigue_test.oscillation_amax_rev_s2;
    config.dwell_time_ms = settings_->fatigue_test.dwell_time_ms;
    config.bounds_method = settings_->fatigue_test.bounds_method_stallguard ? 0 : 1;
    
    // Extended fields for bounds finding (0.0f means "use test unit defaults")
    config.bounds_search_velocity_rpm = settings_->fatigue_test.bounds_search_velocity_rpm;
    config.stallguard_min_velocity_rpm = settings_->fatigue_test.stallguard_min_velocity_rpm;
    config.stall_detection_current_factor = settings_->fatigue_test.stall_detection_current_factor;
    config.bounds_search_accel_rev_s2 = settings_->fatigue_test.bounds_search_accel_rev_s2;

    // SGT (127 means "use test unit default")
    config.stallguard_sgt = settings_->fatigue_test.stallguard_sgt;
    
    ESP_LOGI(TAG_, "Sending config: cycles=%lu, VMAX=%.1f RPM, AMAX=%.1f rev/s², dwell=%lu ms, bounds=%s",
             (unsigned long)config.cycle_amount, config.oscillation_vmax_rpm,
             config.oscillation_amax_rev_s2, (unsigned long)config.dwell_time_ms,
             config.bounds_method == 0 ? "SG" : "ENC");
    
    espnow::SendConfigSet(GetDeviceId(), &config, sizeof(config));
    settings_synced_ = false;
}

void FatigueTester::adjustCurrentValue(int32_t delta) noexcept
{
    if (!settings_) return;
    
    switch (menu_selected_index_) {
        case MENU_CYCLES: {
            int32_t new_val = static_cast<int32_t>(settings_->fatigue_test.cycle_amount) + delta;
            if (new_val < 1) new_val = 1;
            if (new_val > 100000) new_val = 100000;
            settings_->fatigue_test.cycle_amount = static_cast<uint32_t>(new_val);
            break;
        }
        case MENU_DWELL_TIME: {
            int32_t new_val = static_cast<int32_t>(settings_->fatigue_test.dwell_time_ms) + (delta * 100);
            if (new_val < 0) new_val = 0;
            if (new_val > 10000) new_val = 10000;
            settings_->fatigue_test.dwell_time_ms = static_cast<uint32_t>(new_val);
            break;
        }
        case MENU_ERROR_SEVERITY: {
            int32_t new_val = static_cast<int32_t>(settings_->fatigue_test.error_severity_min) + delta;
            if (new_val < 1) new_val = 1;
            if (new_val > 3) new_val = 3;
            settings_->fatigue_test.error_severity_min = static_cast<uint8_t>(new_val);
            break;
        }
    }
}

void FatigueTester::adjustCurrentFloatValue(int32_t delta) noexcept
{
    if (!settings_) return;
    
    float step = menu_edit_step_float_;
    float change = static_cast<float>(delta) * step;
    
    switch (menu_selected_index_) {
        case MENU_VMAX: {
            // oscillation_vmax_rpm: 5-120 RPM, step 5
            float new_val = settings_->fatigue_test.oscillation_vmax_rpm + change;
            if (new_val < 5.0f) new_val = 5.0f;
            if (new_val > 120.0f) new_val = 120.0f;
            settings_->fatigue_test.oscillation_vmax_rpm = new_val;
            break;
        }
        case MENU_AMAX: {
            // oscillation_amax_rev_s2: 0.5-30 rev/s², step 0.5
            float new_val = settings_->fatigue_test.oscillation_amax_rev_s2 + change;
            if (new_val < 0.5f) new_val = 0.5f;
            if (new_val > 30.0f) new_val = 30.0f;
            settings_->fatigue_test.oscillation_amax_rev_s2 = new_val;
            break;
        }
        case MENU_SEARCH_SPEED: {
            // bounds_search_velocity_rpm: 0-300 RPM, step 5
            float new_val = settings_->fatigue_test.bounds_search_velocity_rpm + change;
            if (new_val < 0.0f) new_val = 0.0f;
            if (new_val > 300.0f) new_val = 300.0f;
            settings_->fatigue_test.bounds_search_velocity_rpm = new_val;
            break;
        }
        case MENU_SG_MIN_VEL: {
            // stallguard_min_velocity_rpm: 0-100 RPM, step 1
            float new_val = settings_->fatigue_test.stallguard_min_velocity_rpm + change;
            if (new_val < 0.0f) new_val = 0.0f;
            if (new_val > 100.0f) new_val = 100.0f;
            settings_->fatigue_test.stallguard_min_velocity_rpm = new_val;
            break;
        }
        case MENU_CURRENT_FACTOR: {
            // stall_detection_current_factor: 0.0-1.0, step 0.05
            float new_val = settings_->fatigue_test.stall_detection_current_factor + change;
            if (new_val < 0.0f) new_val = 0.0f;
            if (new_val > 1.0f) new_val = 1.0f;
            settings_->fatigue_test.stall_detection_current_factor = new_val;
            break;
        }
        case MENU_SEARCH_ACCEL: {
            // bounds_search_accel_rev_s2: 0-20 rev/s², step 0.5
            float new_val = settings_->fatigue_test.bounds_search_accel_rev_s2 + change;
            if (new_val < 0.0f) new_val = 0.0f;
            if (new_val > 20.0f) new_val = 20.0f;
            settings_->fatigue_test.bounds_search_accel_rev_s2 = new_val;
            break;
        }
    }
}

void FatigueTester::toggleCurrentChoice() noexcept
{
    if (!settings_) return;
    
    if (menu_selected_index_ == MENU_BOUNDS_MODE) {
        settings_->fatigue_test.bounds_method_stallguard = !settings_->fatigue_test.bounds_method_stallguard;
    } else if (menu_selected_index_ == MENU_FLIP_SCREEN) {
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
    if (menu_selected_index_ == MENU_BACK) {
        menu_active_ = false;
        if (settings_) {
            SettingsStore::Save(*settings_);
            sendSettingsToDevice();
        }
    } else if (menu_selected_index_ == MENU_CYCLES ||
               menu_selected_index_ == MENU_DWELL_TIME ||
               menu_selected_index_ == MENU_ERROR_SEVERITY) {
        // Integer value items
        editing_value_ = true;
        switch (menu_selected_index_) {
            case MENU_CYCLES: menu_edit_step_ = 100; break;
            case MENU_DWELL_TIME: menu_edit_step_ = 100; break;  // ms, step 100
            case MENU_ERROR_SEVERITY: menu_edit_step_ = 1; break;
        }
    } else if (menu_selected_index_ == MENU_VMAX ||
               menu_selected_index_ == MENU_AMAX ||
               menu_selected_index_ == MENU_SEARCH_SPEED ||
               menu_selected_index_ == MENU_SG_MIN_VEL ||
               menu_selected_index_ == MENU_CURRENT_FACTOR ||
               menu_selected_index_ == MENU_SEARCH_ACCEL) {
        // Float value items
        editing_float_ = true;
        switch (menu_selected_index_) {
            case MENU_VMAX: menu_edit_step_float_ = 5.0f; break;
            case MENU_AMAX: menu_edit_step_float_ = 0.5f; break;
            case MENU_SEARCH_SPEED: menu_edit_step_float_ = 5.0f; break;
            case MENU_SG_MIN_VEL: menu_edit_step_float_ = 1.0f; break;
            case MENU_CURRENT_FACTOR: menu_edit_step_float_ = 0.05f; break;
            case MENU_SEARCH_ACCEL: menu_edit_step_float_ = 0.5f; break;
        }
    } else if (menu_selected_index_ == MENU_BOUNDS_MODE ||
               menu_selected_index_ == MENU_FLIP_SCREEN) {
        // Choice items
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
        // Integer value edit screen
        const char* label = "";
        const char* unit = "";
        uint32_t val = 0;
        
        switch (menu_selected_index_) {
            case MENU_CYCLES:
                label = "Cycles";
                unit = "";
                val = settings_->fatigue_test.cycle_amount;
                break;
            case MENU_DWELL_TIME:
                label = "Dwell Time";
                unit = "ms";
                val = settings_->fatigue_test.dwell_time_ms;
                break;
            case MENU_ERROR_SEVERITY:
                label = "Error Severity";
                unit = "";
                val = settings_->fatigue_test.error_severity_min;
                break;
        }
        
            display_->setCursor(0, 20);
        display_->print(label);
            display_->drawLine(0, 29, 128, 29, 1);
            
            // Large value display
            display_->setTextSize(2);
            char buf[32];
        snprintf(buf, sizeof(buf), "%lu%s", (unsigned long)val, unit);
        int16_t x1, y1;
        uint16_t w, h;
        display_->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
        display_->setCursor((128 - w) / 2, 35);
        display_->print(buf);
        
        display_->setTextSize(1);
        display_->setCursor(0, 55);
        display_->print("Rotate: Adjust  OK: Save");
    } else if (editing_float_) {
        // Float value edit screen
        const char* label = "";
        const char* unit = "";
        float val = 0.0f;
        bool is_auto_zero = false;  // For bounds config fields, 0 = use defaults
        
            switch (menu_selected_index_) {
            case MENU_VMAX:
                label = "VMAX";
                unit = "RPM";
                val = settings_->fatigue_test.oscillation_vmax_rpm;
                break;
            case MENU_AMAX:
                label = "AMAX";
                unit = "r/s2";
                val = settings_->fatigue_test.oscillation_amax_rev_s2;
                break;
            case MENU_SEARCH_SPEED:
                label = "Search Speed";
                unit = "RPM";
                val = settings_->fatigue_test.bounds_search_velocity_rpm;
                is_auto_zero = true;
                break;
            case MENU_SG_MIN_VEL:
                label = "SG Min Vel";
                unit = "RPM";
                val = settings_->fatigue_test.stallguard_min_velocity_rpm;
                is_auto_zero = true;
                break;
            case MENU_CURRENT_FACTOR:
                label = "Curr Factor";
                unit = "";
                val = settings_->fatigue_test.stall_detection_current_factor;
                is_auto_zero = true;
                break;
            case MENU_SEARCH_ACCEL:
                label = "Search Accel";
                unit = "r/s2";
                val = settings_->fatigue_test.bounds_search_accel_rev_s2;
                is_auto_zero = true;
                break;
        }
        
        display_->setCursor(0, 20);
        display_->print(label);
        display_->drawLine(0, 29, 128, 29, 1);
        
        // Large value display
        display_->setTextSize(2);
        char buf[32];
        if (is_auto_zero && val == 0.0f) {
            snprintf(buf, sizeof(buf), "AUTO");  // 0 means use defaults
        } else if (menu_selected_index_ == MENU_CURRENT_FACTOR) {
            snprintf(buf, sizeof(buf), "%.2f", val);
        } else if (menu_selected_index_ == MENU_AMAX) {
            snprintf(buf, sizeof(buf), "%.1f%s", val, unit);
        } else {
            snprintf(buf, sizeof(buf), "%.0f%s", val, unit);
        }
            int16_t x1, y1;
            uint16_t w, h;
            display_->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
            display_->setCursor((128 - w) / 2, 35);
            display_->print(buf);
            
            display_->setTextSize(1);
            display_->setCursor(0, 55);
            display_->print("Rotate: Adjust  OK: Save");
    } else if (editing_choice_) {
        // Choice edit screen
        const char* label = "";
        bool* val_ptr = nullptr;
        const char* opt1 = "";
        const char* opt2 = "";
        
        if (menu_selected_index_ == MENU_BOUNDS_MODE) {
            label = "Bounds Mode";
            val_ptr = &settings_->fatigue_test.bounds_method_stallguard;
            opt1 = "[ENC]";
            opt2 = "[STALL]";
        } else if (menu_selected_index_ == MENU_FLIP_SCREEN) {
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
        // Menu list with scrolling - PROTOCOL V2: velocity/acceleration control
        const char* menu_items[] = {
            "Cycles",
            "VMAX (RPM)",
            "AMAX (r/s2)",
            "Dwell (ms)",
            "Bounds Mode",
            "Search Speed",
            "SG Min Vel",
            "Curr Factor",
            "Search Accel",
            "Error Severity",
            "Flip Screen",
            "Back"
        };
        
        // Calculate scroll window (show 4 items at a time)
        int start_idx = menu_selected_index_ - 1;
        if (start_idx < 0) start_idx = 0;
        if (start_idx > MENU_ITEM_COUNT - 4) start_idx = MENU_ITEM_COUNT - 4;
        int end_idx = start_idx + 4;
        
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
                char buf[16];
            const char* str = nullptr;
            
            switch (i) {
                case MENU_CYCLES:
                snprintf(buf, sizeof(buf), "[%lu]", (unsigned long)settings_->fatigue_test.cycle_amount);
                    str = buf;
                    break;
                case MENU_VMAX:
                    snprintf(buf, sizeof(buf), "[%.0f]", settings_->fatigue_test.oscillation_vmax_rpm);
                    str = buf;
                    break;
                case MENU_AMAX:
                    snprintf(buf, sizeof(buf), "[%.1f]", settings_->fatigue_test.oscillation_amax_rev_s2);
                    str = buf;
                    break;
                case MENU_DWELL_TIME:
                    snprintf(buf, sizeof(buf), "[%lu]", (unsigned long)settings_->fatigue_test.dwell_time_ms);
                    str = buf;
                    break;
                case MENU_BOUNDS_MODE:
                    str = settings_->fatigue_test.bounds_method_stallguard ? "[SG]" : "[ENC]";
                    break;
                case MENU_SEARCH_SPEED:
                    if (settings_->fatigue_test.bounds_search_velocity_rpm == 0.0f) {
                        str = "[AUTO]";
                    } else {
                        snprintf(buf, sizeof(buf), "[%.0f]", settings_->fatigue_test.bounds_search_velocity_rpm);
                        str = buf;
                    }
                    break;
                case MENU_SG_MIN_VEL:
                    if (settings_->fatigue_test.stallguard_min_velocity_rpm == 0.0f) {
                        str = "[AUTO]";
                    } else {
                        snprintf(buf, sizeof(buf), "[%.0f]", settings_->fatigue_test.stallguard_min_velocity_rpm);
                        str = buf;
                    }
                    break;
                case MENU_CURRENT_FACTOR:
                    if (settings_->fatigue_test.stall_detection_current_factor == 0.0f) {
                        str = "[AUTO]";
                    } else {
                        snprintf(buf, sizeof(buf), "[%.2f]", settings_->fatigue_test.stall_detection_current_factor);
                        str = buf;
                    }
                    break;
                case MENU_SEARCH_ACCEL:
                    if (settings_->fatigue_test.bounds_search_accel_rev_s2 == 0.0f) {
                        str = "[AUTO]";
                    } else {
                        snprintf(buf, sizeof(buf), "[%.1f]", settings_->fatigue_test.bounds_search_accel_rev_s2);
                        str = buf;
                    }
                    break;
                case MENU_ERROR_SEVERITY:
                snprintf(buf, sizeof(buf), "[%d]", settings_->fatigue_test.error_severity_min);
                    str = buf;
                    break;
                case MENU_FLIP_SCREEN:
                    str = settings_->ui.orientation_flipped ? "[FLIP]" : "[NORM]";
                    break;
                case MENU_BACK:
                    // No value for Back
                    break;
            }
            
            if (str) {
                int16_t x1, y1;
                uint16_t w, h;
                display_->getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
                display_->setCursor(126 - w, y);
                display_->print(str);
            }
            
            y += 12;
        }
        
        // Show scroll indicators
        display_->setTextColor(1);
        if (start_idx > 0) {
            display_->setCursor(120, 12);
            display_->print("^");
        }
        if (end_idx < MENU_ITEM_COUNT) {
            display_->setCursor(120, 56);
            display_->print("v");
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
