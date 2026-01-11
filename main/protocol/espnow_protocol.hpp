/**
 * @file espnow_protocol.hpp
 * @brief Generic ESP-NOW protocol for remote controller with secure pairing
 * 
 * Protocol supports multiple device types with versioning and device ID routing.
 * Includes secure pairing with HMAC mutual authentication.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "espnow_security.hpp"

namespace espnow {

// ============================================================================
// PROTOCOL CONSTANTS
// ============================================================================

static constexpr uint8_t SYNC_BYTE_ = 0xAA;
static constexpr uint8_t PROTOCOL_VERSION_ = 1;
static constexpr uint8_t MAX_PAYLOAD_SIZE_ = 200;
static constexpr uint16_t CRC16_POLYNOMIAL_ = 0x1021;
static constexpr uint8_t WIFI_CHANNEL_ = 1;

// ============================================================================
// MESSAGE TYPES
// ============================================================================

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
    TestComplete,

    // Fatigue-test extensions
    BoundsResult,
    
    // Security / Pairing messages (20-29 range)
    PairingRequest  = 20,
    PairingResponse = 21,
    PairingConfirm  = 22,
    PairingReject   = 23,
    Unpair          = 24,
};

// ============================================================================
// PAIRING STATE
// ============================================================================

enum class PairingState : uint8_t {
    Idle,               ///< Not pairing
    WaitingForResponse, ///< Sent request, waiting for response
    Complete,           ///< Pairing completed successfully
    Failed,             ///< Pairing failed
};

// ============================================================================
// PACKET STRUCTURES
// ============================================================================

#pragma pack(push, 1)
struct EspNowHeader {
    uint8_t sync;
    uint8_t version;
    uint8_t device_id;
    uint8_t type;
    uint8_t id;
    uint8_t len;
};

struct EspNowPacket {
    EspNowHeader hdr;
    uint8_t      payload[MAX_PAYLOAD_SIZE_];
    uint16_t     crc;
};
#pragma pack(pop)

// ============================================================================
// EVENT STRUCTURES
// ============================================================================

struct ProtoEvent {
    MsgType type;
    uint8_t device_id;
    uint8_t sequence_id;
    uint8_t payload[MAX_PAYLOAD_SIZE_];
    size_t payload_len;
    uint8_t src_mac[6];  ///< Source MAC address (for pairing events)
};

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

/**
 * @brief Initialize ESP-NOW with peer storage.
 * @param event_queue Queue to receive ProtoEvent messages
 * @return true on success
 */
bool Init(QueueHandle_t event_queue) noexcept;

/**
 * @brief Send device discovery broadcast.
 */
bool SendDeviceDiscovery() noexcept;

/**
 * @brief Request configuration from a device.
 */
bool SendConfigRequest(uint8_t device_id) noexcept;

/**
 * @brief Send configuration to a device.
 */
bool SendConfigSet(uint8_t device_id, const void* config_data, size_t config_len) noexcept;

/**
 * @brief Send a command to a device.
 */
bool SendCommand(uint8_t device_id, uint8_t command_id, const void* payload, size_t payload_len) noexcept;

// ============================================================================
// PAIRING FUNCTIONS
// ============================================================================

/**
 * @brief Start the pairing process (broadcast discovery).
 * 
 * Sends a PairingRequest to all devices in range. Devices in pairing mode
 * will respond, and the controller will verify their HMAC to ensure they
 * know the shared secret.
 * 
 * @return true if request was sent successfully
 */
bool StartPairing() noexcept;

/**
 * @brief Cancel an in-progress pairing attempt.
 */
void CancelPairing() noexcept;

/**
 * @brief Get the current pairing state.
 */
PairingState GetPairingState() noexcept;

/**
 * @brief Get access to security settings for peer management.
 */
SecuritySettings& GetSecuritySettings() noexcept;

/**
 * @brief Check if a device MAC is approved.
 */
bool IsPeerApproved(const uint8_t mac[6]) noexcept;

/**
 * @brief Manually add an approved peer (bypasses pairing).
 */
bool AddApprovedPeer(const uint8_t mac[6], DeviceType type, const char* name) noexcept;

/**
 * @brief Remove an approved peer.
 */
bool RemoveApprovedPeer(const uint8_t mac[6]) noexcept;

/**
 * @brief Get the number of approved peers.
 */
size_t GetApprovedPeerCount() noexcept;

/**
 * @brief Get the MAC of the active/target device.
 * 
 * Returns the first approved peer of type FatigueTester.
 * 
 * @param mac_out Buffer to receive MAC address (6 bytes)
 * @return true if found
 */
bool GetTargetDeviceMac(uint8_t mac_out[6]) noexcept;

// ============================================================================
// CRC FUNCTION
// ============================================================================

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
