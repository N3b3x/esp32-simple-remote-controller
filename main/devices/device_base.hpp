/**
 * @file device_base.hpp
 * @brief Base device interface for remote controller
 */

#pragma once

#include <cstdint>
#include "../button.hpp"
#include "../protocol/espnow_protocol.hpp"
#include "../components/Adafruit_SH1106_ESPIDF/Adafruit_SH1106.h"
#include "../components/EC11_Encoder/inc/ec11_encoder.hpp"
#include "../settings.hpp"

class DeviceBase {
public:
    virtual ~DeviceBase() = default;
    
    // Public functions: PascalCase
    virtual uint8_t GetDeviceId() const noexcept = 0;
    virtual const char* GetDeviceName() const noexcept = 0;
    virtual void RenderMainScreen() noexcept = 0;
    virtual void HandleButton(ButtonId button_id) noexcept = 0;
    virtual void HandleEncoder(EC11Encoder::Direction direction) noexcept = 0;
    virtual void HandleEncoderButton(bool pressed) noexcept = 0;
    virtual void UpdateFromProtocol(const espnow::ProtoEvent& event) noexcept = 0;
    virtual bool IsConnected() const noexcept;
    virtual void RequestStatus() noexcept = 0;
    
    // Settings menu support
    virtual void BuildSettingsMenu(class MenuBuilder& builder) noexcept = 0;
    
protected:
    // Protected members for derived classes
    DeviceBase(Adafruit_SH1106* display, Settings* settings) noexcept;
    
    Adafruit_SH1106* display_;
    Settings* settings_;
    
protected:
    // Protected helper to update connection status
    void updateConnectionStatus() noexcept;
    
    // Member variables: snake_case + trailing underscore
    bool connected_;
    uint32_t last_status_tick_;
};

