/**
 * @file espnow_protocol.hpp
 * @brief Generic ESP-NOW protocol for remote controller
 * 
 * Protocol supports multiple device types with versioning and device ID routing.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"

namespace espnow {

// Constants: UPPER_CASE + trailing underscore
static constexpr uint8_t SYNC_BYTE_ = 0xAA;
static constexpr uint8_t PROTOCOL_VERSION_ = 1;
static constexpr uint8_t MAX_PAYLOAD_SIZE_ = 200;
static constexpr uint16_t CRC16_POLYNOMIAL_ = 0x1021;
static constexpr uint8_t WIFI_CHANNEL_ = 1;

// Enum class: PascalCase
enum class MsgType : uint8_t {
    DeviceDiscovery = 1,
    DeviceInfo,
    ConfigRequest,
    ConfigResponse,
    ConfigSet,
    ConfigAck,
    Command,
    CommandAck,
    StatusUpdate,
    Error,
    ErrorClear,
    TestComplete
};

// Struct: PascalCase
#pragma pack(push, 1)
struct EspNowHeader {
    uint8_t sync;        // Always SYNC_BYTE_
    uint8_t version;     // Protocol version
    uint8_t device_id;   // Device type ID
    uint8_t type;        // MsgType enum
    uint8_t id;          // Sequence ID
    uint8_t len;         // Payload length
};

// Full packet
struct EspNowPacket {
    EspNowHeader hdr;
    uint8_t      payload[MAX_PAYLOAD_SIZE_];
    uint16_t     crc;  // CRC over [hdr + payload[0..len-1]]
};
#pragma pack(pop)

// Event structure for higher layers
struct ProtoEvent {
    MsgType type;
    uint8_t device_id;
    uint8_t sequence_id;
    uint8_t payload[MAX_PAYLOAD_SIZE_];
    size_t payload_len;
};

// Public functions: PascalCase
bool Init(QueueHandle_t event_queue) noexcept;
bool SendDeviceDiscovery() noexcept;
bool SendConfigRequest(uint8_t device_id) noexcept;
bool SendConfigSet(uint8_t device_id, const void* config_data, size_t config_len) noexcept;
bool SendCommand(uint8_t device_id, uint8_t command_id, const void* payload, size_t payload_len) noexcept;

// CRC16-CCITT function
inline uint16_t crc16_ccitt(const uint8_t* data, size_t len) noexcept
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ CRC16_POLYNOMIAL_;
            else
                crc <<= 1;
        }
    }
    return crc;
}

} // namespace espnow

