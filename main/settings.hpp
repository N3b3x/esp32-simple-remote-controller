/**
 * @file settings.hpp
 * @brief Settings storage for remote controller
 */

#pragma once

#include "protocol/device_protocols.hpp"

// Settings structure for fatigue test device
struct FatigueTestSettings {
    uint32_t cycle_amount   = 1000;
    uint32_t time_per_cycle = 1;    // seconds
    uint32_t dwell_time     = 1;    // seconds
    bool     bounds_method_stallguard = true; // true = stallguard, false = encoder
    uint8_t  error_severity_min = 1; // Minimum error severity to display (1=low, 2=medium, 3=high)
};

// UI board settings - stored locally, never synchronized with devices
struct UISettings {
    bool orientation_flipped = false;
    uint8_t last_ui_state = 0;      // Last UI state before sleep (UiState enum value)
    uint8_t last_device_id = 0;      // Last selected device ID before sleep
    // Note: sleep timestamp is stored in RTC memory (RTC_DATA_ATTR) for persistence
    // Future UI settings can be added here (e.g., brightness, contrast, etc.)
};

// Complete settings structure
struct Settings {
    FatigueTestSettings fatigue_test;  // Fatigue test device settings
    UISettings          ui;             // UI board settings (local only)
};

namespace SettingsStore {

void Init(Settings& s) noexcept;
void Save(const Settings& s) noexcept;

} // namespace SettingsStore

