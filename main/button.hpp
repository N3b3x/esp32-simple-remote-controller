/**
 * @file button.hpp
 * @brief Button handling for UI board
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

enum class ButtonId {
    Back,      // Navigate back
    Confirm    // Critical action confirmation
};

struct ButtonEvent {
    ButtonId id;
};

namespace Buttons {

bool Init(QueueHandle_t evt_queue) noexcept;
void ConfigureWakeup() noexcept;  // for deep sleep wake

} // namespace Buttons

