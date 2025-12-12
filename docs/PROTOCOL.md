# ESP-NOW Protocol Specification - Remote Controller

## Overview

The ESP-NOW protocol provides generic wireless communication between the remote controller and multiple device types. It supports protocol versioning, device ID routing, and extensible message types.

## Protocol Version

**Current Version**: 1

The protocol header includes a version field for future compatibility. Devices should check version and handle version mismatches gracefully.

## Packet Format

### Header Structure

```
┌─────────────────────────────────────────────────────────┐
│              EspNowHeader (6 bytes)                    │
├──────┬──────┬──────────┬──────┬──────┬─────────────────┤
│ Sync │ Ver  │ DeviceID │ Type │  ID  │ Length          │
│(1B)  │(1B)  │   (1B)   │(1B)  │(1B)  │  (1B)          │
└──────┴──────┴──────────┴──────┴──────┴─────────────────┘
```

**Field Descriptions**:
- **Sync**: Always `0xAA` (sync byte)
- **Ver**: Protocol version (currently `1`)
- **DeviceID**: Device type identifier
  - `0` = Broadcast (all devices)
  - `1` = Fatigue Tester
  - `2` = Mock Device
  - `3+` = Reserved for future devices
- **Type**: Message type (see MsgType enum)
- **ID**: Sequence ID (increments per message, wraps at 255)
- **Length**: Payload length (0-200 bytes)

### Full Packet Structure

```
┌─────────────────────────────────────────────────────────┐
│ EspNowHeader (6 bytes)                                  │
├─────────────────────────────────────────────────────────┤
│ Payload (0-200 bytes, variable length)                 │
├─────────────────────────────────────────────────────────┤
│ CRC16 (2 bytes, CRC16-CCITT over header + payload)     │
└─────────────────────────────────────────────────────────┘
```

**Total Packet Size**: 8-208 bytes (6 header + 0-200 payload + 2 CRC)

## Message Types

### Discovery Messages

| Type | Value | Direction | Description |
|------|-------|-----------|-------------|
| `DeviceDiscovery` | 1 | Controller → Device | Broadcast to discover devices |
| `DeviceInfo` | 2 | Device → Controller | Device information response |

### Configuration Messages

| Type | Value | Direction | Description |
|------|-------|-----------|-------------|
| `ConfigRequest` | 3 | Controller → Device | Request current configuration |
| `ConfigResponse` | 4 | Device → Controller | Send current configuration |
| `ConfigSet` | 5 | Controller → Device | Set new configuration |
| `ConfigAck` | 6 | Device → Controller | Acknowledge config set |

### Command Messages

| Type | Value | Direction | Description |
|------|-------|-----------|-------------|
| `Command` | 7 | Controller → Device | Send command to device |
| `CommandAck` | 8 | Device → Controller | Acknowledge command |

### Status Messages

| Type | Value | Direction | Description |
|------|-------|-----------|-------------|
| `StatusUpdate` | 9 | Device → Controller | Periodic status update |
| `Error` | 10 | Device → Controller | Error notification |
| `TestComplete` | 11 | Device → Controller | Test/operation complete |

## Device IDs

| ID | Device Name | Description |
|----|-------------|-------------|
| 0 | Broadcast | All devices (for discovery) |
| 1 | Fatigue Tester | Fatigue test unit |
| 2 | Mock Device | Mock device for testing |
| 3+ | Reserved | Future devices |

## Device-Specific Payloads

### Fatigue Tester

**Config Payload** (`FatigueTestConfigPayload`):
```cpp
struct FatigueTestConfigPayload {
    uint32_t cycle_amount;        // Target cycles
    uint32_t time_per_cycle_sec;  // Seconds per cycle
    uint32_t dwell_time_sec;      // Dwell time at bounds
    uint8_t  bounds_method;       // 0 = StallGuard, 1 = Encoder
};
```

**Status Payload** (`FatigueTestStatusPayload`):
```cpp
struct FatigueTestStatusPayload {
    uint32_t cycle_number;  // Current cycle
    uint8_t  state;         // TestState enum
    uint8_t  err_code;      // Error code if state == Error
};
```

**Commands** (`FatigueTestCommand`):
- `Start = 1`
- `Pause = 2`
- `Resume = 3`
- `Stop = 4`
- `SetConfig = 5`
- `RequestConfig = 6`
- `RequestStatus = 7`

### Mock Device

**Config Payload** (`MockDeviceConfigPayload`):
```cpp
struct MockDeviceConfigPayload {
    uint32_t param1;
    uint32_t param2;
    bool     enable_feature;
};
```

**Status Payload** (`MockDeviceStatusPayload`):
```cpp
struct MockDeviceStatusPayload {
    uint32_t value1;
    uint32_t value2;
    float    temperature;
    bool     status_flag;
};
```

## Communication Flow

### Device Discovery

```
Controller                          Device
    │                                  │
    │─── DeviceDiscovery (ID=0) ──────▶│
    │                                  │
    │◀── DeviceInfo (ID=1, name=...) ─│
    │                                  │
```

### Configuration Flow

```
Controller                          Device
    │                                  │
    │─── ConfigRequest (ID=1) ────────▶│
    │                                  │
    │◀── ConfigResponse (ID=1, data) ─│
    │                                  │
    │─── ConfigSet (ID=1, data) ──────▶│
    │                                  │
    │◀── ConfigAck (ID=1, ok) ─────────│
    │                                  │
```

### Command Flow

```
Controller                          Device
    │                                  │
    │─── Command (ID=1, cmd=Start) ───▶│
    │                                  │
    │◀── CommandAck (ID=1, ok) ───────│
    │                                  │
    │◀── StatusUpdate (ID=1, status) ─│
    │                                  │
```

## CRC16-CCITT Calculation

Same as fatigue test unit protocol. See `PROTOCOL.md` in fatigue_test_espnow for details.

## WiFi Channel

**Default Channel**: 1

Both devices must use the same WiFi channel.

## MAC Address Configuration

The controller must be configured with the target device's MAC address in `config.hpp`:

```cpp
static constexpr uint8_t TEST_UNIT_MAC_[6] = { 0x24, 0x6F, 0x28, 0x00, 0x00, 0x01 };
```

## Protocol Extensibility

### Adding New Device Types

1. **Assign Device ID**: Choose unused ID (3+)
2. **Define Payloads**: Add structures to `device_protocols.hpp`
3. **Implement Device**: Create device class inheriting from `DeviceBase`
4. **Register Device**: Add to device registry

### Adding New Message Types

1. **Add to Enum**: Add to `MsgType` enum in `espnow_protocol.hpp`
2. **Update Handlers**: Update protocol handlers in both controller and device
3. **Define Payload**: Create payload structure if needed

## Version Compatibility

### Version Checking

- Devices should check protocol version in header
- Mismatched versions should be handled gracefully
- Future versions may add new fields or message types

### Backward Compatibility

- Version 1 devices should work with version 1 controller
- New message types should be optional
- Unknown message types should be ignored (with logging)

## Error Handling

### Protocol Errors

- **Invalid Sync Byte**: Packet rejected
- **Version Mismatch**: Packet rejected, error logged
- **CRC Mismatch**: Packet rejected
- **Invalid Device ID**: Packet rejected

### Application Errors

- **Command Failed**: Device sends `Error` message
- **Config Invalid**: Device sends `ConfigAck` with error code
- **Timeout**: Application-level timeout handling

## Performance

- **Latency**: < 10ms typical
- **Throughput**: Sufficient for control messages
- **Range**: ~100-200m line-of-sight
- **Reliability**: High (CRC validation, sequence IDs)

## Security Considerations

- **No Encryption**: Current protocol has no encryption
- **MAC Filtering**: Can be added at ESP-NOW level
- **Future**: Encryption support planned

