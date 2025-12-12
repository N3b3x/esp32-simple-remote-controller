/**
 * @file device_base.cpp
 * @brief Base device implementation
 */

#include "device_base.hpp"
#include "../components/Adafruit_SH1106_ESPIDF/Adafruit_SH1106.h"
#include "../settings.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

DeviceBase::DeviceBase(Adafruit_SH1106* display, Settings* settings) noexcept
    : display_(display)
    , settings_(settings)
    , connected_(false)
    , last_status_tick_(0)
{
}

void DeviceBase::updateConnectionStatus() noexcept
{
    TickType_t now = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(5000); // 5 second timeout
    
    if (now - last_status_tick_ > timeout_ticks) {
        connected_ = false;
    }
}

bool DeviceBase::IsConnected() const noexcept
{
    return connected_;
}

