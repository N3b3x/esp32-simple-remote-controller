/**
 * @file fatigue_tester.hpp
 * @brief Fatigue test device implementation with full menu and screen support
 */

#pragma once

#include "device_base.hpp"
#include "../protocol/device_protocols.hpp"

class FatigueTester : public DeviceBase {
public:
    FatigueTester(class Adafruit_SH1106* display, class Settings* settings) noexcept;
    
    // Public functions: PascalCase
    uint8_t GetDeviceId() const noexcept override;
    const char* GetDeviceName() const noexcept override;
    void RenderMainScreen() noexcept override;
    void RenderControlScreen() noexcept;
    void HandleButton(ButtonId button_id) noexcept override;
    void HandleEncoder(EC11Encoder::Direction direction) noexcept override;
    void HandleEncoderButton(bool pressed) noexcept override;
    void UpdateFromProtocol(const espnow::ProtoEvent& event) noexcept override;
    bool IsConnected() const noexcept override;
    void RequestStatus() noexcept override;
    void BuildSettingsMenu(class MenuBuilder& builder) noexcept override;
    
    // Additional rendering for settings menu
    void RenderSettingsMenu() noexcept;
    void RenderPopup() noexcept;
    
    // Menu state
    bool IsMenuActive() const noexcept { return menu_active_; }
    void SetMenuActive(bool active) noexcept { menu_active_ = active; }
    
    // Popup state
    bool IsPopupActive() const noexcept { return popup_active_; }
    
private:
    // Private functions: camelCase
    void renderStatusScreen() noexcept;
    void handleStatusUpdate(const device_protocols::FatigueTestStatusPayload& status) noexcept;
    void sendSettingsToDevice() noexcept;
    void adjustCurrentValue(int32_t delta) noexcept;
    void toggleCurrentChoice() noexcept;
    void handleMenuEnter() noexcept;
    void renderErrorFooter() noexcept;
    void addError(uint8_t code, uint8_t severity) noexcept;
    void clearErrors() noexcept;
    void checkConfirmHold(ButtonId button_id) noexcept;
    
    // Member variables: snake_case + trailing underscore
    device_protocols::FatigueTestState current_state_;
    uint32_t current_cycle_;
    uint8_t error_code_;
    bool popup_active_;
    uint8_t popup_type_; // 1=start, 2=stop
    bool popup_yes_selected_;
    bool settings_synced_;
    
    // Menu state
    bool menu_active_;
    int menu_selected_index_;
    bool editing_value_;
    bool editing_choice_;
    uint32_t menu_edit_step_;
    
    // Error handling
    struct ErrorEntry {
        uint8_t code;
        uint8_t severity; // 1=low, 2=medium, 3=high
        TickType_t timestamp;
    };
    static constexpr size_t MAX_ERRORS_ = 3;
    ErrorEntry errors_[MAX_ERRORS_];
    size_t error_count_;
    TickType_t confirm_hold_start_;
    bool confirm_held_;
};

