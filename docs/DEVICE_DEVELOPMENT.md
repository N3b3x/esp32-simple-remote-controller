# Device Development Guide

## Overview

This guide explains how to add new device types to the remote controller system. The system uses a device abstraction layer that allows easy addition of new device types.

## Architecture

### Device Abstraction

All devices inherit from `DeviceBase`, which provides:
- Display access
- Settings access
- Protocol event handling
- Menu building interface

### Device Registry

The `device_registry` namespace manages:
- Device ID assignment
- Device creation (factory pattern)
- Device name lookup
- Available device list

## Step-by-Step Guide

### Step 1: Define Device ID

Add device ID constant to `devices/device_registry.hpp`:

```cpp
namespace device_registry {
    static constexpr uint8_t DEVICE_ID_MY_DEVICE_ = 3;  // Next available ID
    // ...
}
```

### Step 2: Define Protocol Payloads

Add device-specific payloads to `protocol/device_protocols.hpp`:

```cpp
namespace device_protocols {

// My Device payloads
#pragma pack(push, 1)
struct MyDeviceConfigPayload {
    uint32_t setting1;
    uint16_t setting2;
    bool     enable_feature;
};

struct MyDeviceStatusPayload {
    float    temperature;
    uint32_t counter;
    bool     is_active;
};

enum class MyDeviceCommand : uint8_t {
    Activate = 1,
    Deactivate,
    Reset,
    RequestStatus
};

enum class MyDeviceState : uint8_t {
    Idle = 0,
    Active,
    Error
};
#pragma pack(pop)

} // namespace device_protocols
```

### Step 3: Create Device Class

Create `devices/my_device.hpp`:

```cpp
#pragma once

#include "device_base.hpp"
#include "../protocol/device_protocols.hpp"

class MyDevice : public DeviceBase {
public:
    MyDevice(Adafruit_SH1106* display, Settings* settings) noexcept;
    ~MyDevice() override = default;

    // DeviceBase overrides
    uint8_t GetDeviceId() const noexcept override;
    const char* GetDeviceName() const noexcept override;
    void RenderMainScreen() noexcept override;
    void RenderControlScreen() noexcept override;
    void HandleButton(ButtonId button_id) noexcept override;
    void HandleEncoder(EC11Encoder::Direction direction) noexcept override;
    void HandleEncoderButton(bool pressed) noexcept override;
    void UpdateFromProtocol(const espnow::ProtoEvent& event) noexcept override;
    bool IsConnected() const noexcept override;
    void RequestStatus() noexcept override;
    void BuildSettingsMenu(MenuBuilder& builder) noexcept override;

private:
    // Private members
    device_protocols::MyDeviceStatusPayload current_status_;
    device_protocols::MyDeviceState current_state_;
};
```

### Step 4: Implement Device Class

Create `devices/my_device.cpp`:

```cpp
#include "my_device.hpp"
#include "../menu/menu_system.hpp"
#include "../protocol/espnow_protocol.hpp"
#include "esp_log.h"
#include <cstdio>

static const char* TAG_MY_DEVICE_ = "MyDevice";

MyDevice::MyDevice(Adafruit_SH1106* display, Settings* settings) noexcept
    : DeviceBase(display, settings),
      current_state_(device_protocols::MyDeviceState::Idle) {
    current_status_ = {0.0f, 0, false};
}

uint8_t MyDevice::GetDeviceId() const noexcept {
    return device_registry::DEVICE_ID_MY_DEVICE_;
}

const char* MyDevice::GetDeviceName() const noexcept {
    return "My Device";
}

void MyDevice::RenderMainScreen() noexcept {
    if (!display_) return;
    
    display_->clearDisplay();
    display_->setTextSize(1);
    display_->setTextColor(1);
    display_->setCursor(0, 0);
    
    display_->print("My Device\n");
    char buf[64];
    snprintf(buf, sizeof(buf), "Temp: %.1fC\n", current_status_.temperature);
    display_->print(buf);
    snprintf(buf, sizeof(buf), "Count: %lu\n", current_status_.counter);
    display_->print(buf);
    display_->print(current_status_.is_active ? "Active: Yes\n" : "Active: No\n");
    
    display_->display();
}

void MyDevice::RenderControlScreen() noexcept {
    if (!display_) return;
    
    display_->clearDisplay();
    display_->setTextSize(1);
    display_->setTextColor(1);
    display_->setCursor(0, 0);
    
    display_->print("Control\n");
    display_->print("CONFIRM: Activate\n");
    display_->print("BACK: Return\n");
    
    display_->display();
}

void MyDevice::HandleButton(ButtonId button_id) noexcept {
    switch (button_id) {
        case ButtonId::Confirm:
            // Send activate command
            espnow::SendCommand(GetDeviceId(), 
                              static_cast<uint8_t>(device_protocols::MyDeviceCommand::Activate),
                              nullptr, 0);
            break;
        case ButtonId::Back:
            // Handled by UI controller
            break;
        default:
            break;
    }
}

void MyDevice::HandleEncoder(EC11Encoder::Direction direction) noexcept {
    // Handle encoder rotation if needed
    (void)direction;
}

void MyDevice::HandleEncoderButton(bool pressed) noexcept {
    if (pressed) {
        // Handle encoder button press
    }
}

void MyDevice::UpdateFromProtocol(const espnow::ProtoEvent& event) noexcept {
    if (event.device_id != GetDeviceId()) return;
    
    switch (event.type) {
        case espnow::MsgType::StatusUpdate:
            if (event.payload_len >= sizeof(device_protocols::MyDeviceStatusPayload)) {
                std::memcpy(&current_status_, event.payload, 
                           sizeof(device_protocols::MyDeviceStatusPayload));
            }
            break;
        case espnow::MsgType::ConfigResponse:
            // Handle config response
            break;
        default:
            break;
    }
}

bool MyDevice::IsConnected() const noexcept {
    return DeviceBase::IsConnected();
}

void MyDevice::RequestStatus() noexcept {
    espnow::SendCommand(GetDeviceId(),
                       static_cast<uint8_t>(device_protocols::MyDeviceCommand::RequestStatus),
                       nullptr, 0);
}

void MyDevice::BuildSettingsMenu(MenuBuilder& builder) noexcept {
    // Build device-specific settings menu
    // Example: Add value items, choice items, etc.
    // This is called by the menu system when entering device settings
}
```

### Step 5: Register Device

Update `devices/device_registry.cpp`:

```cpp
#include "my_device.hpp"

// In CreateDevice function:
case DEVICE_ID_MY_DEVICE_:
    return std::make_unique<MyDevice>(display, settings);

// In GetDeviceName function:
case DEVICE_ID_MY_DEVICE_:
    return "My Device";
```

### Step 6: Add to Build System

Update `main/CMakeLists.txt`:

```cmake
# Add source file
list(APPEND COMPONENT_SRCS
    # ... existing files ...
    "devices/my_device.cpp"
)
```

## Best Practices

### 1. Display Rendering

- Always check `display_` pointer before use
- Use `clearDisplay()` before rendering
- Call `display()` after rendering
- Use `snprintf()` for formatted text (no `printf()` on display)

### 2. Protocol Handling

- Check `device_id` matches before processing
- Validate payload length before copying
- Handle all message types your device uses
- Send appropriate acknowledgments

### 3. Settings Menu

- Use `MenuBuilder` to add menu items
- Point directly to settings structure members
- Provide appropriate min/max/step values
- Handle menu navigation in device code if needed

### 4. Error Handling

- Log errors with appropriate log level
- Handle missing display gracefully
- Validate protocol payloads
- Return appropriate error codes

### 5. Memory Management

- Use stack allocation when possible
- Avoid large buffers
- Be mindful of ESP32-C6 memory constraints

## Example: Complete Device Implementation

See `devices/fatigue_tester.hpp/cpp` and `devices/mock_device.hpp/cpp` for complete examples.

## Testing

### Unit Testing

1. Create device instance
2. Test rendering methods
3. Test protocol handling
4. Test menu building

### Integration Testing

1. Flash controller with new device
2. Verify device appears in selection
3. Test all screens and navigation
4. Test protocol communication
5. Test settings persistence

## Troubleshooting

### Device Not Appearing

- Check device ID is registered
- Verify `CreateDevice()` case is added
- Check build includes device source file

### Protocol Not Working

- Verify device ID matches in protocol
- Check payload structures match
- Verify message types are handled

### Display Issues

- Check display pointer is valid
- Verify display initialization
- Check text rendering (use `print()`, not `printf()`)

### Menu Issues

- Verify `BuildSettingsMenu()` is implemented
- Check menu items are properly added
- Verify value pointers are valid

