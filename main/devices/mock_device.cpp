/**
 * @file mock_device.cpp
 * @brief Mock device implementation
 */

#include "mock_device.hpp"
#include "../devices/device_registry.hpp"
#include "../components/EC11_Encoder/inc/ec11_encoder.hpp"
#include "../components/Adafruit_SH1106_ESPIDF/Adafruit_SH1106.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>

MockDevice::MockDevice(Adafruit_SH1106* display, Settings* settings) noexcept
    : DeviceBase(display, settings)
    , value1_(0)
    , value2_(0)
    , temperature_(25.0f)
    , status_flag_(false)
{
}

uint8_t MockDevice::GetDeviceId() const noexcept
{
    return device_registry::DEVICE_ID_MOCK_;
}

const char* MockDevice::GetDeviceName() const noexcept
{
    return "Mock Device";
}

void MockDevice::RenderMainScreen() noexcept
{
    if (!display_) return;
    
    display_->clearDisplay();
    display_->setTextSize(1);
    display_->setTextColor(1);
    display_->setCursor(0, 0);
    
    display_->print("Mock Device\n");
    char buf[64];
    snprintf(buf, sizeof(buf), "Value1: %lu\n", (unsigned long)value1_);
    display_->print(buf);
    snprintf(buf, sizeof(buf), "Value2: %lu\n", (unsigned long)value2_);
    display_->print(buf);
    snprintf(buf, sizeof(buf), "Temp: %.1fC\n", temperature_);
    display_->print(buf);
    display_->print(status_flag_ ? "Status: ON\n" : "Status: OFF\n");
    
    display_->display();
}

void MockDevice::HandleButton(ButtonId button_id) noexcept
{
    (void)button_id; // TODO: Implement button handling
}

void MockDevice::HandleEncoder(EC11Encoder::Direction direction) noexcept
{
    (void)direction; // TODO: Implement encoder handling
}

void MockDevice::HandleEncoderButton(bool pressed) noexcept
{
    (void)pressed; // TODO: Implement encoder button handling
}

void MockDevice::UpdateFromProtocol(const espnow::ProtoEvent& event) noexcept
{
    (void)event; // TODO: Implement protocol handling
}

bool MockDevice::IsConnected() const noexcept
{
    return connected_;
}

void MockDevice::RequestStatus() noexcept
{
    // Simulate connection
    connected_ = true;
    last_status_tick_ = xTaskGetTickCount();
}

void MockDevice::BuildSettingsMenu(class MenuBuilder& builder) noexcept
{
    (void)builder; // TODO: Implement settings menu building
}

