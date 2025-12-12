/**
 * @file ui_state.hpp
 * @brief UI state definitions for remote controller
 */

#pragma once

enum class UiState {
    Splash,            // Initial splash screen (ConMed™, Test Devices, Remote Control)
    DeviceSelection,   // Device selection screen (choose device to control)
    DeviceMain,        // Device-specific main screen
    DeviceSettings,    // Device settings menu
    DeviceControl,     // Device control screen (start/pause/stop)
    Popup             // Confirmation dialogs
};

