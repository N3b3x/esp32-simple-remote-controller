/**
 * @file settings.hpp
 * @brief Settings storage for remote controller
 */

#pragma once

#include "protocol/device_protocols.hpp"

/**
 * @brief Settings structure for fatigue test device.
 * 
 * @details
 * Base settings (cycle_amount, time_per_cycle, dwell_time, bounds_method)
 * are always synchronized with the test unit.
 * 
 * Extended settings (bounds_search_velocity_rpm, etc.) are optional:
 * - Value of 0.0f means "use test unit's default from test config"
 * - Non-zero values override the test unit's defaults
 */
struct FatigueTestSettings {
    // Base settings (always synced)
    uint32_t cycle_amount   = 1000;
    uint32_t time_per_cycle = 5;    // seconds
    uint32_t dwell_time     = 1;    // seconds
    bool     bounds_method_stallguard = true; // true = stallguard, false = encoder
    
    // Extended settings (0.0f = use test unit defaults)
    float    bounds_search_velocity_rpm = 0.0f;       // Search speed during bounds finding (RPM)
    float    stallguard_min_velocity_rpm = 0.0f;      // Minimum velocity for StallGuard2 (RPM)
    float    stall_detection_current_factor = 0.0f;   // Current reduction factor (0.0-1.0)
    float    bounds_search_accel_rev_s2 = 0.0f;       // Acceleration during bounds finding (rev/s²)
    
    // UI-only settings (not synced to device)
    uint8_t  error_severity_min = 1; // Minimum error severity to display (1=low, 2=medium, 3=high)
};

/**
 * @brief UI board settings - stored locally, never synchronized with devices.
 */
struct UISettings {
    bool orientation_flipped = false;
    uint8_t last_ui_state = 0;      // Last UI state before sleep (UiState enum value)
    uint8_t last_device_id = 0;      // Last selected device ID before sleep
    // Note: sleep timestamp is stored in RTC memory (RTC_DATA_ATTR) for persistence
    // Future UI settings can be added here (e.g., brightness, contrast, etc.)
};

/**
 * @brief Complete settings structure.
 */
struct Settings {
    FatigueTestSettings fatigue_test;  // Fatigue test device settings
    UISettings          ui;             // UI board settings (local only)
};

namespace SettingsStore {

void Init(Settings& s) noexcept;
void Save(const Settings& s) noexcept;

} // namespace SettingsStore
