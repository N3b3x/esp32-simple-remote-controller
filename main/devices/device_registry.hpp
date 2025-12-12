/**
 * @file device_registry.hpp
 * @brief Device registry and factory for remote controller
 */

#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include "device_base.hpp"

namespace device_registry {

// Constants: UPPER_CASE + trailing underscore
static constexpr uint8_t MAX_DEVICES_ = 16;
static constexpr uint8_t DEVICE_ID_FATIGUE_TESTER_ = 1;
static constexpr uint8_t DEVICE_ID_MOCK_ = 2;

// Public functions: PascalCase
std::unique_ptr<DeviceBase> CreateDevice(uint8_t device_id, 
                                         class Adafruit_SH1106* display,
                                         class Settings* settings) noexcept;
const std::vector<uint8_t>& GetAvailableDeviceIds() noexcept;
const char* GetDeviceName(uint8_t device_id) noexcept;

} // namespace device_registry

