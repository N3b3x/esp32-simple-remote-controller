/**
 * @file ui_controller.cpp
 * @brief UI controller implementation
 */

#include "ui_controller.hpp"
#include "../devices/device_registry.hpp"
#include "../devices/device_base.hpp"
#include "../devices/fatigue_tester.hpp"
#include "../settings.hpp"
#include "../button.hpp"
#include "../protocol/espnow_protocol.hpp"
#include "../components/Adafruit_SH1106_ESPIDF/Adafruit_SH1106.h"
#include "../components/EC11_Encoder/inc/ec11_encoder.hpp"
#include "../components/Adafruit_BusIO_ESPIDF/Wire.h"
#include "../components/Adafruit_BusIO_ESPIDF/Adafruit_I2CDevice.h"
#include "../config.hpp"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_private/esp_clk.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <memory>

static const char* TAG_ = "UiController";

// RTC memory to persist across deep sleep - stores RTC time when entering sleep
RTC_DATA_ATTR static uint64_t s_sleep_rtc_time_us = 0;

static Adafruit_SH1106* s_display_ = nullptr;
static EC11Encoder* s_encoder_ = nullptr;

// Engineering humor quotes for splash screen (one-liners only, max ~20 chars)
namespace {
    static const char* const SPLASH_QUOTES_[] = {
        "Test. Verify. Repeat.",
        "Quality in Control",
        "Ready to Test",
        "Test Mode: ON",
        "Let's Test This!",
        "Testing... 1, 2, 3",
        "Engineers Never Panic",
        "They Analyze",
        "Failure is Prototype",
        "Keep Calm & Test",
        "Remote-ly Awesome",
        "Test Time!",
        "Quality Assured",
        "Press to Test",
        "Test. Don't Guess.",
        "Let's Torque About It",
        "I Run on AC/DC",
        "Too Many Voltage Jokes",
        "Energy Efficient",
        "Not Lazy",
        "Calculated Humor",
        "Watt's Up?",
        "Ohm My Goodness",
        "Make It Possible",
        "Engineers Create",
        "Science Finds Way",
        "Innovation Starts",
        "Precision Matters",
        "Details Make Perfect",
        "Build It Right",
        "Test Everything",
        "Quality First",
        "Excellence in Test",
        "Test. Verify. Done.",
        "Quality Control",
        "Test Driven",
        "Verify Everything",
        "Test with Purpose",
        "Quality Matters",
        "Test Smart",
        "Engineered Right",
        "Tested & Verified"
    };
    
    static constexpr size_t NUM_SPLASH_QUOTES_ = sizeof(SPLASH_QUOTES_) / sizeof(SPLASH_QUOTES_[0]);
}

// External queues from main (declared in main.cpp)
extern QueueHandle_t g_button_queue_;
extern QueueHandle_t g_proto_queue_;

bool UiController::Init(QueueHandle_t ui_queue, Settings* settings, 
                        uint32_t* inactivity_ticks_ptr) noexcept
{
    ui_queue_ = ui_queue;
    settings_ = settings;
    last_activity_tick_ = inactivity_ticks_ptr;
    selected_device_id_ = 0;
    popup_active_ = false;
    last_encoder_button_state_ = false;
    last_encoder_pos_ = 0;
    
    // Initialize display first (needed for device creation)
    Adafruit_I2CDevice::setDefaultPins(OLED_SDA_PIN_, OLED_SCL_PIN_);
    Adafruit_I2CDevice::setDefaultFrequency(OLED_I2C_FREQ_);
    
    s_display_ = new Adafruit_SH1106(OLED_WIDTH_, OLED_HEIGHT_, &Wire, -1, OLED_I2C_ADDR_);
    vTaskDelay(pdMS_TO_TICKS(50));
    if (!s_display_->begin(OLED_I2C_ADDR_, true)) {
        ESP_LOGE(TAG_, "Failed to initialize OLED display");
        return false;
    }
    
    if (settings_ && settings_->ui.orientation_flipped) {
        s_display_->setRotation(2);
    }
    
    // Initialize encoder
    s_encoder_ = new EC11Encoder(ENCODER_TRA_PIN_, ENCODER_TRB_PIN_, ENCODER_PSH_PIN_, 
                                 ENCODER_PULSES_PER_REV_);
    if (!s_encoder_->begin()) {
        ESP_LOGE(TAG_, "Failed to initialize encoder");
        return false;
    }
    
    // Now that display is initialized, check if we're waking from sleep and restore last state
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    bool waking_from_sleep = (wakeup_cause != ESP_SLEEP_WAKEUP_UNDEFINED);
    bool should_restore_state = false;
    
    if (waking_from_sleep && settings_ && settings_->ui.last_ui_state > 0 && s_sleep_rtc_time_us > 0) {
        // Calculate sleep duration using RTC time (persists across deep sleep)
        uint64_t current_rtc_time_us = esp_clk_rtc_time();
        uint64_t sleep_duration_us = 0;
        
        // Handle RTC time wraparound (unlikely but possible)
        if (current_rtc_time_us >= s_sleep_rtc_time_us) {
            sleep_duration_us = current_rtc_time_us - s_sleep_rtc_time_us;
        } else {
            // Wraparound occurred - assume long sleep and show splash
            ESP_LOGW(TAG_, "RTC time wraparound detected, showing splash");
            sleep_duration_us = UINT64_MAX; // Force splash screen
        }
        
        // Convert to seconds for threshold comparison
        uint64_t sleep_duration_sec = sleep_duration_us / 1000000ULL;
        uint64_t threshold_sec = SLEEP_RESET_THRESHOLD_SEC_;
        
        ESP_LOGI(TAG_, "Sleep duration: %llu seconds (threshold: %llu)", 
                 (unsigned long long)sleep_duration_sec, (unsigned long long)threshold_sec);
        
        // Restore state only if sleep was less than threshold
        bool should_restore = (sleep_duration_us < (threshold_sec * 1000000ULL));
        
        if (should_restore) {
            should_restore_state = true;
        } else {
            ESP_LOGI(TAG_, "Sleep duration exceeds threshold, showing splash instead of restoring state");
        }
    }
    
    if (should_restore_state) {
        // Restore last state
        current_state_ = static_cast<UiState>(settings_->ui.last_ui_state);
        selected_device_id_ = settings_->ui.last_device_id;
        
        // Restore device if we were on a device screen
        if (selected_device_id_ > 0 && 
            (current_state_ == UiState::DeviceMain || 
             current_state_ == UiState::DeviceSettings || 
             current_state_ == UiState::DeviceControl)) {
            current_device_ = device_registry::CreateDevice(selected_device_id_, s_display_, settings_);
            if (!current_device_) {
                // Device creation failed, fall back to device selection
                current_state_ = UiState::DeviceSelection;
                selected_device_id_ = 0;
            }
        } else if (current_state_ == UiState::DeviceSelection) {
            // Restore device selection state
            const auto& device_ids = device_registry::GetAvailableDeviceIds();
            if (selected_device_id_ == 0 && !device_ids.empty()) {
                selected_device_id_ = device_ids[0];
            }
        } else {
            // Invalid state, fall back to splash
            current_state_ = UiState::Splash;
            selected_device_id_ = 0;
        }
        ESP_LOGI(TAG_, "Restored state: %d, device: %d", 
                 static_cast<int>(current_state_), selected_device_id_);
    } else {
        // Long sleep, cold boot, or no saved state - show splash screen
        current_state_ = UiState::Splash;
        selected_device_id_ = 0;
        // Clear sleep timestamp
        s_sleep_rtc_time_us = 0;
        
        if (waking_from_sleep) {
            ESP_LOGI(TAG_, "Woke from sleep but showing splash (long sleep or no saved state)");
        } else {
            ESP_LOGI(TAG_, "Cold boot - showing splash screen");
        }
    }
    
    // Auto-select first device if only one available (for device selection)
    const auto& device_ids = device_registry::GetAvailableDeviceIds();
    if (current_state_ == UiState::DeviceSelection) {
        if (selected_device_id_ == 0) {
            if (device_ids.size() == 1) {
                selected_device_id_ = device_ids[0];
            } else if (!device_ids.empty()) {
                // Default to first device for selection (ensures something is selected)
                selected_device_id_ = device_ids[0];
            }
        }
        // Ensure encoder position is synced with selection on next render
    }
    
    ESP_LOGI(TAG_, "UI Controller initialized (state: %d, device: %d)", 
             static_cast<int>(current_state_), selected_device_id_);
    return true;
}

void UiController::Task(void* arg) noexcept
{
    (void)arg;
    
    // Initial render of splash screen
    renderCurrentScreen();
    
    // Event handling - UI queue now contains ButtonEvent directly, or we check protocol queue
    ButtonEvent button_evt{};
    espnow::ProtoEvent proto_evt{};
    
    // Initialize encoder position tracking based on current selection
    if (s_encoder_ && current_state_ == UiState::DeviceSelection) {
        const auto& device_ids = device_registry::GetAvailableDeviceIds();
        if (!device_ids.empty() && selected_device_id_ > 0) {
            // Find current selection index and sync encoder position
            size_t current_idx = 0;
            for (size_t i = 0; i < device_ids.size(); ++i) {
                if (device_ids[i] == selected_device_id_) {
                    current_idx = i;
                    break;
                }
            }
            // Set encoder position to match selection index
            resetEncoderTracking(static_cast<int32_t>(current_idx));
        }
    }
    
    while (true) {
        // Process encoder events (event-based handling for reliable navigation)
        processEncoderEvents();
        
        // Check for button events from UI queue (forwarded by button_task)
        if (xQueueReceive(ui_queue_, &button_evt, pdMS_TO_TICKS(50)) == pdTRUE) {
            handleButton(button_evt);
            // renderCurrentScreen() is called by transitionToState() or explicitly in handleButton
        }
        
        // Check for protocol events (separate queue)
        if (xQueueReceive(g_proto_queue_, &proto_evt, 0) == pdTRUE) {
            handleProtocol(proto_evt);
            renderCurrentScreen();
        }

        // Periodic UI refresh (keeps dynamic/timed UI elements updating even without input events).
        // This is especially important for the FatigueTester control screen which shows live data
        // and uses brief visual flashes (e.g., NOT CONNECTED).
        {
            static TickType_t last_render_tick = 0;
            TickType_t now = xTaskGetTickCount();
            const TickType_t refresh_period = pdMS_TO_TICKS(250);
            if ((now - last_render_tick) > refresh_period) {
                if (current_device_ &&
                    (current_state_ == UiState::DeviceMain || current_state_ == UiState::DeviceControl)) {
                    renderCurrentScreen();
                }
                last_render_tick = now;
            }
        }

        // Keepalive / polling: nudge devices so UI stays up-to-date even if the test unit
        // is not streaming status frequently.
        {
            static TickType_t last_poll_tick = 0;
            TickType_t now = xTaskGetTickCount();
            const TickType_t poll_period = pdMS_TO_TICKS(1000);
            if (current_device_ && (now - last_poll_tick) > poll_period) {
                // Only poll while we're on a device screen.
                if (current_state_ == UiState::DeviceMain ||
                    current_state_ == UiState::DeviceControl ||
                    current_state_ == UiState::DeviceSettings) {
                    current_device_->RequestStatus();
                }
                last_poll_tick = now;
            }
        }
        
        // If no events, check if we need to refresh
        // (e.g., for animations or status updates)
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void UiController::PrepareForSleep() noexcept
{
    // Save current state before sleep
    if (settings_) {
        settings_->ui.last_ui_state = static_cast<uint8_t>(current_state_);
        settings_->ui.last_device_id = selected_device_id_;
        SettingsStore::Save(*settings_);
    }
    
    // Save RTC time when entering sleep (persists across deep sleep)
    // esp_clk_rtc_time() returns RTC time in microseconds, which continues during deep sleep
    s_sleep_rtc_time_us = esp_clk_rtc_time();
    
    if (s_display_) {
        s_display_->clearDisplay();
        s_display_->setTextSize(1);
        s_display_->setTextColor(1);
        s_display_->setCursor(0, 0);
        s_display_->print("Sleeping...\n");
        s_display_->display();
    }
}

void UiController::handleButton(const ButtonEvent& event) noexcept
{
    // Add debouncing to prevent accidental double-presses
    static TickType_t last_button_time[2] = {0, 0}; // One for each button type
    TickType_t now = xTaskGetTickCount();
    const TickType_t min_button_interval = pdMS_TO_TICKS(300); // 300ms minimum between presses
    
    int button_idx = (event.id == ButtonId::Back) ? 0 : 1;
    if (now - last_button_time[button_idx] < min_button_interval) {
        return; // Ignore if too soon after last press
    }
    last_button_time[button_idx] = now;
    
    if (last_activity_tick_) {
        *last_activity_tick_ = xTaskGetTickCount();
    }
    
    // Device screens handle buttons for navigation, but UI controller manages state transitions
    // Device-specific button actions can be handled in device, but navigation is here
    
    switch (current_state_) {
        case UiState::Splash:
            // Any button press goes to device selection
            transitionToState(UiState::DeviceSelection);
            break;
        case UiState::DeviceSelection:
            if (event.id == ButtonId::Back) {
                // Go back to splash screen
                selected_device_id_ = 0;
                transitionToState(UiState::Splash);
            } else if (event.id == ButtonId::Confirm) {
                // Select current device and go to main screen
                const auto& device_ids = device_registry::GetAvailableDeviceIds();
                if (selected_device_id_ == 0 && !device_ids.empty()) {
                    // Auto-select first device if none selected
                    selected_device_id_ = device_ids[0];
                }
                if (selected_device_id_ > 0) {
                    current_device_ = device_registry::CreateDevice(selected_device_id_, s_display_, settings_);
                    if (current_device_) {
                        transitionToState(UiState::DeviceMain);
                        // Reset encoder position when entering device screen
                        resetEncoderTracking(0);
                    }
                }
            }
            break;
        case UiState::DeviceMain:
            // Check for popup first
            if (current_device_ && 
                current_device_->GetDeviceId() == device_registry::DEVICE_ID_FATIGUE_TESTER_) {
                FatigueTester* ft = static_cast<FatigueTester*>(current_device_.get());
                if (ft->IsPopupActive()) {
                    // Handle popup
                    current_device_->HandleButton(event.id);
                    renderCurrentScreen();
                    return;
                }
            }
            
            if (event.id == ButtonId::Back) {
                transitionToState(UiState::DeviceSelection);
                current_device_.reset();
                selected_device_id_ = 0;
            } else if (event.id == ButtonId::Confirm) {
                transitionToState(UiState::DeviceControl);
            }
            break;
        case UiState::DeviceControl:
            // Route button to device first (for popup and menu handling)
            if (current_device_) {
                // Check if this is a FatigueTester and if popup is active
                bool popup_was_active = false;
                if (current_device_->GetDeviceId() == device_registry::DEVICE_ID_FATIGUE_TESTER_) {
                    FatigueTester* ft = static_cast<FatigueTester*>(current_device_.get());
                    popup_was_active = ft->IsPopupActive();
                }
                
                // Handle device-specific buttons
                current_device_->HandleButton(event.id);
                
                // Check if popup is still active after handling
                bool popup_still_active = false;
                if (current_device_->GetDeviceId() == device_registry::DEVICE_ID_FATIGUE_TESTER_) {
                    FatigueTester* ft = static_cast<FatigueTester*>(current_device_.get());
                    popup_still_active = ft->IsPopupActive();
                }
                
                // If popup was active, render screen
                if (popup_was_active || popup_still_active) {
                    renderCurrentScreen();
                } else if (event.id == ButtonId::Back) {
                    // Back button returns to main screen
                    transitionToState(UiState::DeviceMain);
                } else {
                    renderCurrentScreen();
                }
            } else {
                // Fallback: if device is null, Back button goes to DeviceMain
                if (event.id == ButtonId::Back) {
                    transitionToState(UiState::DeviceMain);
                }
            }
            break;
        case UiState::DeviceSettings:
            // Route button to device for menu handling
            if (current_device_) {
                bool menu_was_active = false;
                if (current_device_->GetDeviceId() == device_registry::DEVICE_ID_FATIGUE_TESTER_) {
                    FatigueTester* ft = static_cast<FatigueTester*>(current_device_.get());
                    menu_was_active = ft->IsMenuActive();
                }
                
                current_device_->HandleButton(event.id);
                
                // Check if menu was exited
                bool menu_still_active = false;
                if (current_device_->GetDeviceId() == device_registry::DEVICE_ID_FATIGUE_TESTER_) {
                    FatigueTester* ft = static_cast<FatigueTester*>(current_device_.get());
                    menu_still_active = ft->IsMenuActive();
                }
                
                if (menu_was_active && !menu_still_active) {
                    // Menu was exited, go back to DeviceMain
                    // Add delay to ensure I2C bus is ready before state transition
                    vTaskDelay(pdMS_TO_TICKS(20));
                    transitionToState(UiState::DeviceMain);
                } else if (event.id == ButtonId::Back && !menu_still_active) {
                    // Back button pressed and menu is not active - go back to DeviceMain
                    // (handles case where menu wasn't active to begin with)
                    // Add delay to ensure I2C bus is ready before state transition
                    vTaskDelay(pdMS_TO_TICKS(20));
                    transitionToState(UiState::DeviceMain);
                } else {
                    renderCurrentScreen();
                }
            } else {
                // Fallback: if device is null, Back button goes to DeviceMain
                if (event.id == ButtonId::Back) {
                    transitionToState(UiState::DeviceMain);
                }
            }
            break;
        default:
            break;
    }
}

void UiController::handleEncoderButton(bool pressed) noexcept
{
    if (!pressed) return; // Only handle press, not release
    
    if (last_activity_tick_) {
        *last_activity_tick_ = xTaskGetTickCount();
    }
    
    // Add debouncing for encoder button to prevent accidental double-presses
    static TickType_t last_encoder_button_time = 0;
    TickType_t now = xTaskGetTickCount();
    const TickType_t min_button_interval = pdMS_TO_TICKS(300); // 300ms minimum between presses
    
    if (now - last_encoder_button_time < min_button_interval) {
        return; // Ignore if too soon after last press
    }
    last_encoder_button_time = now;
    
    // Encoder button handling for device screens
    if (current_device_ &&
        (current_state_ == UiState::DeviceControl ||
         current_state_ == UiState::DeviceSettings)) {
        // Device-specific encoder button actions for control and settings screens.
        //
        // IMPORTANT: For the FatigueTester settings menu, selecting the "Back" item is done
        // via the encoder button. In that case the device will set menu_active_=false, and
        // we must transition the UI state back to DeviceMain (otherwise the user gets stuck
        // on the settings screen even though the menu was exited and settings were saved).

        bool menu_was_active = false;
        if (current_state_ == UiState::DeviceSettings &&
            current_device_->GetDeviceId() == device_registry::DEVICE_ID_FATIGUE_TESTER_) {
            FatigueTester* ft = static_cast<FatigueTester*>(current_device_.get());
            menu_was_active = ft->IsMenuActive();
        }

        current_device_->HandleEncoderButton(pressed);

        if (current_state_ == UiState::DeviceSettings &&
            menu_was_active &&
            current_device_->GetDeviceId() == device_registry::DEVICE_ID_FATIGUE_TESTER_) {
            FatigueTester* ft = static_cast<FatigueTester*>(current_device_.get());
            if (!ft->IsMenuActive()) {
                vTaskDelay(pdMS_TO_TICKS(20));
                transitionToState(UiState::DeviceMain);
                return;
            }
        }

        renderCurrentScreen();
        return;
    }
    
    // DeviceMain state: encoder button goes to settings
    if (current_device_ && current_state_ == UiState::DeviceMain) {
        if (current_device_->GetDeviceId() == device_registry::DEVICE_ID_FATIGUE_TESTER_) {
            FatigueTester* ft = static_cast<FatigueTester*>(current_device_.get());
            ft->SetMenuActive(true);
        }
        transitionToState(UiState::DeviceSettings);
        return;
    }
    
    switch (current_state_) {
        case UiState::Splash:
            // Encoder button press goes to device selection
            transitionToState(UiState::DeviceSelection);
            break;
        case UiState::DeviceSelection:
            // Encoder button click selects the device (same as Confirm button)
            {
                const auto& device_ids = device_registry::GetAvailableDeviceIds();
                if (selected_device_id_ == 0 && !device_ids.empty()) {
                    // Auto-select first device if none selected
                    selected_device_id_ = device_ids[0];
                }
                if (selected_device_id_ > 0) {
                    current_device_ = device_registry::CreateDevice(selected_device_id_, s_display_, settings_);
                    if (current_device_) {
                        transitionToState(UiState::DeviceMain);
                        // Reset encoder position when entering device screen
                        resetEncoderTracking(0);
                    }
                }
            }
            break;
        default:
            break;
    }
}

void UiController::handleProtocol(const espnow::ProtoEvent& event) noexcept
{
    if (current_device_) {
        current_device_->UpdateFromProtocol(event);
    }
}

void UiController::renderCurrentScreen() noexcept
{
    switch (current_state_) {
        case UiState::Splash:
            renderSplashScreen();
            break;
        case UiState::DeviceSelection:
            renderDeviceSelectionScreen();
            break;
        case UiState::DeviceMain:
            renderDeviceMainScreen();
            break;
        case UiState::DeviceSettings:
            renderDeviceSettingsScreen();
            break;
        case UiState::DeviceControl:
            renderDeviceControlScreen();
            break;
        case UiState::Popup:
            // Check if device has popup
            if (current_device_ && 
                current_device_->GetDeviceId() == device_registry::DEVICE_ID_FATIGUE_TESTER_) {
                FatigueTester* ft = static_cast<FatigueTester*>(current_device_.get());
                ft->RenderPopup();
            } else {
                renderPopup();
            }
            break;
    }
}

void UiController::transitionToState(UiState new_state) noexcept
{
    current_state_ = new_state;
    if (current_device_) {
        ESP_LOGI(TAG_, "UI state -> %d (device_id=%d)", (int)current_state_, (int)current_device_->GetDeviceId());
    } else {
        ESP_LOGI(TAG_, "UI state -> %d", (int)current_state_);
    }
    renderCurrentScreen();
}

void UiController::renderSplashScreen() noexcept
{
    if (!s_display_) return;
    
    s_display_->clearDisplay();
    
    // Calculate centered positions
    // Display is 128x64
    // Text size 1: 6x8 pixels per character
    // Text size 2: 12x16 pixels per character (2x scaling)
    
    s_display_->setTextColor(1);
    
    int16_t x1, y1;
    uint16_t w, h;
    
    // "ConMed" - centered, text size 2 (larger, more prominent)
    s_display_->setTextSize(2);
    const char* conmed_text = "ConMed";
    s_display_->getTextBounds(conmed_text, 0, 0, &x1, &y1, &w, &h);
    int16_t conmed_x = (OLED_WIDTH_ - w) / 2;
    int16_t conmed_y = 4; // Top margin (reduced to make room)
    
    s_display_->setCursor(conmed_x, conmed_y);
    s_display_->print(conmed_text);
    
    // Add trademark symbol (smaller, superscript style)
    s_display_->setTextSize(1);
    s_display_->setCursor(conmed_x + w + 2, conmed_y + 2);
    s_display_->print("TM");
    
    // "Test Devices" - centered, moved down to avoid overlap with ConMed
    // Text size 2 is ~16 pixels tall, so we need more spacing
    const char* test_devices = "Test Devices";
    s_display_->getTextBounds(test_devices, 0, 0, &x1, &y1, &w, &h);
    int16_t test_devices_x = (OLED_WIDTH_ - w) / 2;
    int16_t test_devices_y = conmed_y + 18; // Increased spacing (16px for text size 2 + 2px margin)
    s_display_->setCursor(test_devices_x, test_devices_y);
    s_display_->print(test_devices);
    
    // "Remote Control" - centered
    const char* remote_control = "Remote Control";
    s_display_->getTextBounds(remote_control, 0, 0, &x1, &y1, &w, &h);
    int16_t remote_control_x = (OLED_WIDTH_ - w) / 2;
    int16_t remote_control_y = test_devices_y + h + 2; // Reduced spacing to fit better
    s_display_->setCursor(remote_control_x, remote_control_y);
    s_display_->print(remote_control);
    
    // Horizontal line
    int16_t line_y = remote_control_y + h + 6; // Reduced spacing
    s_display_->drawLine(10, line_y, 118, line_y, 1);
    
    // Random engineering humor quote (one-liner)
    // Use RTC time as seed for pseudo-random selection (changes each boot)
    uint64_t rtc_time = esp_clk_rtc_time();
    size_t quote_index = (rtc_time / 1000000ULL) % NUM_SPLASH_QUOTES_; // Use seconds as seed
    const char* quote = SPLASH_QUOTES_[quote_index];
    
    s_display_->getTextBounds(quote, 0, 0, &x1, &y1, &w, &h);
    int16_t quote_x = (OLED_WIDTH_ - w) / 2;
    int16_t quote_y = line_y + 6; // Reduced spacing
    s_display_->setCursor(quote_x, quote_y);
    s_display_->print(quote);
    
    s_display_->display();
}

void UiController::renderDeviceSelectionScreen() noexcept
{
    if (!s_display_) return;
    
    s_display_->clearDisplay();
    s_display_->setTextSize(1);
    s_display_->setTextColor(1);
    
    // Title
    s_display_->setCursor(0, 0);
    s_display_->print("Select Device:");
    
    // Draw line below title
    s_display_->drawLine(0, 9, 128, 9, 1);
    
    // Get available devices
    const auto& device_ids = device_registry::GetAvailableDeviceIds();
    
    // Display device list
    int16_t y_pos = 12;
    for (size_t i = 0; i < device_ids.size() && y_pos < 64; ++i) {
        uint8_t device_id = device_ids[i];
        const char* device_name = device_registry::GetDeviceName(device_id);
        
        // Highlight selected device
        if (device_id == selected_device_id_) {
            // Draw selection indicator
            s_display_->fillRect(0, y_pos - 1, 128, 10, 1);
            s_display_->setTextColor(0); // Inverted text
        } else {
            s_display_->setTextColor(1);
        }
        
        s_display_->setCursor(4, y_pos);
        s_display_->print("> ");
        s_display_->print(device_name);
        
        if (device_id == selected_device_id_) {
            s_display_->setTextColor(1); // Reset for next item
        }
        
        y_pos += 12;
    }
    
    s_display_->display();
}

void UiController::renderDeviceMainScreen() noexcept
{
    if (!s_display_) return; // Safety check
    
    if (current_device_) {
        // Add small delay to ensure I2C bus is ready
        vTaskDelay(pdMS_TO_TICKS(10));
        current_device_->RenderMainScreen();
    }
}

void UiController::renderDeviceSettingsScreen() noexcept
{
    if (!s_display_) return; // Safety check
    
    if (current_device_) {
        // Check if this is a FatigueTester
        if (current_device_->GetDeviceId() == device_registry::DEVICE_ID_FATIGUE_TESTER_) {
            FatigueTester* ft = static_cast<FatigueTester*>(current_device_.get());
            // Add small delay to ensure I2C bus is ready
            vTaskDelay(pdMS_TO_TICKS(10));
            ft->RenderSettingsMenu();
        } else {
            // Generic device settings screen
            s_display_->clearDisplay();
            s_display_->setTextSize(1);
            s_display_->setTextColor(1);
            s_display_->setCursor(0, 0);
            s_display_->print("Settings");
            s_display_->drawLine(0, 9, 128, 9, 1);
            s_display_->setCursor(0, 12);
            s_display_->print("No settings available");
            s_display_->display();
        }
    }
}

void UiController::renderDeviceControlScreen() noexcept
{
    if (current_device_) {
        // Check if this is a FatigueTester
        if (current_device_->GetDeviceId() == device_registry::DEVICE_ID_FATIGUE_TESTER_) {
            FatigueTester* ft = static_cast<FatigueTester*>(current_device_.get());
            // Check if popup is active - if so, render popup instead
            if (ft->IsPopupActive()) {
                ft->RenderPopup();
            } else {
                ft->RenderControlScreen();
            }
        } else {
            // Generic device control screen
            if (s_display_) {
                s_display_->clearDisplay();
                s_display_->setTextSize(1);
                s_display_->setTextColor(1);
                s_display_->setCursor(0, 0);
                s_display_->print("Control");
                s_display_->drawLine(0, 9, 128, 9, 1);
                s_display_->setCursor(0, 12);
                s_display_->print("Press CONFIRM");
                s_display_->setCursor(0, 24);
                s_display_->print("to start");
                s_display_->display();
            }
        }
    }
}

void UiController::renderPopup() noexcept
{
    // TODO: Implement popup rendering
}

void UiController::resetEncoderTracking(int32_t position) noexcept
{
    if (s_encoder_) {
        s_encoder_->setPosition(position);
        last_encoder_pos_ = position;
        
        // Drain any pending encoder events to prevent stale events from causing issues
        EC11Encoder::Event evt;
        while (s_encoder_->getEventQueue() && 
               xQueueReceive(s_encoder_->getEventQueue(), &evt, 0) == pdTRUE) {
            // Discard pending events
        }
    }
}

void UiController::processEncoderEvents() noexcept
{
    if (!s_encoder_) return;
    
    // Check encoder button state (direct polling for button is fine)
    bool current_encoder_button = s_encoder_->isButtonPressed();
    if (current_encoder_button && !last_encoder_button_state_) {
        handleEncoderButton(true);
        renderCurrentScreen();
    }
    last_encoder_button_state_ = current_encoder_button;
    
    // Process encoder rotation events from the event queue
    // This gives us reliable, event-based handling without division artifacts
    EC11Encoder::Event evt;
    bool had_rotation = false;
    
    while (s_encoder_->getEventQueue() && 
           xQueueReceive(s_encoder_->getEventQueue(), &evt, 0) == pdTRUE) {
        
        if (evt.type == EC11Encoder::EventType::ROTATION) {
            had_rotation = true;
            
            if (current_state_ == UiState::DeviceSelection) {
                // Device selection navigation
                const auto& device_ids = device_registry::GetAvailableDeviceIds();
                if (!device_ids.empty()) {
                    // Find current selection index
                    size_t current_idx = 0;
                    for (size_t i = 0; i < device_ids.size(); ++i) {
                        if (device_ids[i] == selected_device_id_) {
                            current_idx = i;
                            break;
                        }
                    }
                    
                    // Navigate based on rotation direction
                    if (evt.direction == EC11Encoder::Direction::CW && 
                        current_idx < device_ids.size() - 1) {
                        // CW moves down (next item)
                        selected_device_id_ = device_ids[current_idx + 1];
                    } else if (evt.direction == EC11Encoder::Direction::CCW && 
                               current_idx > 0) {
                        // CCW moves up (previous item)
                        selected_device_id_ = device_ids[current_idx - 1];
                    }
                }
            } else if (current_device_ && 
                      (current_state_ == UiState::DeviceMain || 
                       current_state_ == UiState::DeviceSettings || 
                       current_state_ == UiState::DeviceControl)) {
                // Device screen encoder handling (for menu navigation)
                current_device_->HandleEncoder(evt.direction);
            }
            
            // Update tracking position
            last_encoder_pos_ = evt.position;
        } else if (evt.type == EC11Encoder::EventType::BUTTON && evt.button_pressed) {
            // Button press event from queue (alternative to polling)
            handleEncoderButton(true);
        }
    }
    
    // Render if we processed any rotation events
    if (had_rotation) {
        renderCurrentScreen();
    }
}

