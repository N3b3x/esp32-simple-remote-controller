/**
 * @file device_registry.cpp
 * @brief Device registry implementation
 */

#include "device_registry.hpp"
#include "device_base.hpp"
#include "fatigue_tester.hpp"
#include "mock_device.hpp"
#include "../components/Adafruit_SH1106_ESPIDF/Adafruit_SH1106.h"
#include "../settings.hpp"
#include <vector>
#include <memory>

namespace device_registry {

static std::vector<uint8_t> s_available_device_ids_ = {
    DEVICE_ID_FATIGUE_TESTER_,
    DEVICE_ID_MOCK_
};

std::unique_ptr<DeviceBase> CreateDevice(uint8_t device_id, 
                                         Adafruit_SH1106* display,
                                         Settings* settings) noexcept
{
    switch (device_id) {
        case DEVICE_ID_FATIGUE_TESTER_:
            return std::make_unique<FatigueTester>(display, settings);
        case DEVICE_ID_MOCK_:
            return std::make_unique<MockDevice>(display, settings);
        default:
            return nullptr;
    }
}

const std::vector<uint8_t>& GetAvailableDeviceIds() noexcept
{
    return s_available_device_ids_;
}

const char* GetDeviceName(uint8_t device_id) noexcept
{
    switch (device_id) {
        case DEVICE_ID_FATIGUE_TESTER_:
            return "Fatigue Tester";
        case DEVICE_ID_MOCK_:
            return "Mock Device";
        default:
            return "Unknown";
    }
}

} // namespace device_registry

