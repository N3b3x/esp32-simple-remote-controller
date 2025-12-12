/**
 * @file mock_device.hpp
 * @brief Mock device for demonstration
 */

#pragma once

#include "device_base.hpp"

class MockDevice : public DeviceBase {
public:
    MockDevice(class Adafruit_SH1106* display, class Settings* settings) noexcept;
    
    // Public functions: PascalCase
    uint8_t GetDeviceId() const noexcept override;
    const char* GetDeviceName() const noexcept override;
    void RenderMainScreen() noexcept override;
    void HandleButton(ButtonId button_id) noexcept override;
    void HandleEncoder(EC11Encoder::Direction direction) noexcept override;
    void HandleEncoderButton(bool pressed) noexcept override;
    void UpdateFromProtocol(const espnow::ProtoEvent& event) noexcept override;
    bool IsConnected() const noexcept override;
    void RequestStatus() noexcept override;
    void BuildSettingsMenu(class MenuBuilder& builder) noexcept override;
    
private:
    // Member variables: snake_case + trailing underscore
    uint32_t value1_;
    uint32_t value2_;
    float temperature_;
    bool status_flag_;
};

