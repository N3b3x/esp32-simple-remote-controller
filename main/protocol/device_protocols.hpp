/**
 * @file device_protocols.hpp
 * @brief Device-specific protocol definitions
 * 
 * Contains payload structures for different device types.
 * These structures must match the corresponding definitions in each device's firmware.
 */

#pragma once

#include <cstdint>
#include "../protocol/espnow_protocol.hpp"

// Device-specific payload structures
namespace device_protocols {

// ============================================================================
// Fatigue Test Device Payloads
// ============================================================================

/**
 * @brief Configuration payload for fatigue test device.
 * 
 * @details
 * This structure is sent in CONFIG_SET messages and received in CONFIG_RESPONSE.
 * Extended fields (float parameters) are optional - older firmware versions
 * may not support them. When sending, always send the full structure.
 * When receiving, check the payload length to determine if extended fields
 * are present.
 * 
 * PROTOCOL V2: Uses direct velocity/acceleration control instead of cycle time.
 */
#pragma pack(push, 1)
struct FatigueTestConfigPayload {
    // Base fields (17 bytes) - required, always present
    uint32_t cycle_amount;                     ///< Target number of test cycles (0 = infinite)
    float    oscillation_vmax_rpm;             ///< Max oscillation velocity (RPM) - direct to TMC5160 VMAX
    float    oscillation_amax_rev_s2;          ///< Oscillation acceleration (rev/s²) - direct to TMC5160 AMAX
    uint32_t dwell_time_ms;                    ///< Dwell time at endpoints (milliseconds)
    uint8_t  bounds_method;                    ///< 0 = StallGuard, 1 = Encoder
    
    // Extended fields (16 bytes) - optional, for bounds finding configuration
    float    bounds_search_velocity_rpm;       ///< Search speed during bounds finding (RPM)
    float    stallguard_min_velocity_rpm;      ///< Minimum velocity threshold for StallGuard2 (RPM)
    float    stall_detection_current_factor;   ///< Current reduction factor (0.0-1.0)
    float    bounds_search_accel_rev_s2;       ///< Acceleration during bounds finding (rev/s²)

    // Extended v2 field (optional)
    // StallGuard threshold (SGT). Valid range is typically [-64, 63].
    // 127 means "use test unit default".
    int8_t   stallguard_sgt;
};

/**
 * @brief Status update payload for fatigue test device.
 */
struct FatigueTestStatusPayload {
    uint32_t cycle_number;  ///< Current cycle count
    uint8_t  state;         ///< FatigueTestState enum value
    uint8_t  err_code;      ///< Error code if state == Error
};

/**
 * @brief Command payload for fatigue test device.
 */
struct FatigueTestCommandPayload {
    uint8_t command_id;  ///< 1=Start, 2=Pause, 3=Resume, 4=Stop
};

/**
 * @brief Test state enumeration for fatigue test device.
 * 
 * Per coding standards: PascalCase for enum values (state types).
 */
enum class FatigueTestState : uint8_t {
    Idle = 0,
    Running,
    Paused,
    Completed,
    Error
};

/**
 * @brief Command ID enumeration for fatigue test device.
 * 
 * Per coding standards: PascalCase for enum values.
 */
enum class FatigueTestCommandId : uint8_t {
    Start = 1,
    Pause = 2,
    Resume = 3,
    Stop = 4
};
#pragma pack(pop)

// Size constants for payload validation
static constexpr size_t FATIGUE_TEST_CONFIG_BASE_SIZE = 17;     ///< Base config payload size (without extended fields)
static constexpr size_t FATIGUE_TEST_CONFIG_FULL_SIZE = sizeof(FatigueTestConfigPayload);  ///< Full config payload size

// ============================================================================
// Mock Device Payloads (for demonstration/testing)
// ============================================================================

#pragma pack(push, 1)
struct MockDeviceConfigPayload {
    uint32_t param1;
    uint32_t param2;
    bool     enable_feature;
};

struct MockDeviceStatusPayload {
    uint32_t value1;
    uint32_t value2;
    float    temperature;
    bool     status_flag;
};
#pragma pack(pop)

} // namespace device_protocols
