/**
 * @file device_protocols.hpp
 * @brief Device-specific protocol definitions
 * 
 * Contains payload structures for different device types.
 */

#pragma once

#include <cstdint>
#include "../protocol/espnow_protocol.hpp"

// Device-specific payload structures
namespace device_protocols {

// Fatigue test device payloads
#pragma pack(push, 1)
struct FatigueTestConfigPayload {
    uint32_t cycle_amount;
    uint32_t time_per_cycle_sec;
    uint32_t dwell_time_sec;
    uint8_t  bounds_method;      // 0 = stallguard, 1 = encoder
};

struct FatigueTestStatusPayload {
    uint32_t cycle_number;
    uint8_t  state;      // TestState enum
    uint8_t  err_code;   // if state == ERROR
};

struct FatigueTestCommandPayload {
    uint8_t command_id;  // 1=START, 2=PAUSE, 3=RESUME, 4=STOP
};

enum class FatigueTestState : uint8_t {
    Idle = 0,
    Running,
    Paused,
    Completed,
    Error
};
#pragma pack(pop)

// Mock device payloads (for demonstration)
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

