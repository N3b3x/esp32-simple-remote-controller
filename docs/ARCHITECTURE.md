# ESP32 Remote Controller - Architecture Documentation

## Overview

The ESP32 Remote Controller is a generic remote control framework that supports multiple device types through a unified ESP-NOW protocol. It features a device abstraction layer, dynamic menu system, and OLED display interface.

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                  Remote Controller Application               │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐ │
│  │   Button     │    │   Encoder    │    │   ESP-NOW     │ │
│  │   Handler    │───▶│   Handler    │───▶│   Protocol    │ │
│  │              │    │              │    │              │ │
│  └──────────────┘    └──────────────┘    └──────┬───────┘ │
│         │                  │                      │          │
│         └──────────────────┴──────────────────────┘          │
│                            │                                  │
│                   ┌────────▼────────┐                         │
│                   │  UI Controller  │                         │
│                   │  (State Machine)│                         │
│                   └────────┬────────┘                         │
│                            │                                  │
│         ┌──────────────────┴──────────────────┐              │
│         │                                      │              │
│  ┌──────▼────────┐                    ┌───────▼──────┐      │
│  │   Device      │                    │    Menu      │      │
│  │   Registry    │                    │    System    │      │
│  │               │                    │              │      │
│  │ ┌──────────┐  │                    └───────┬──────┘      │
│  │ │Fatigue   │  │                            │              │
│  │ │Tester    │  │                    ┌───────▼──────┐      │
│  │ └──────────┘  │                    │   Display    │      │
│  │ ┌──────────┐  │                    │   (SH1106)   │      │
│  │ │Mock      │  │                    └──────────────┘      │
│  │ │Device    │  │                                            │
│  │ └──────────┘  │                                            │
│  └───────────────┘                                            │
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

## Component Overview

### 1. UI Controller

**Files**: `ui/ui_controller.hpp/cpp`, `ui/ui_state.hpp`

- **Purpose**: Main UI state machine and coordination
- **States**: Splash, DeviceSelection, DeviceMain, DeviceSettings, DeviceControl, Popup
- **Features**:
  - State machine management
  - Device selection
  - Screen rendering coordination
  - Event routing

**State Flow**:
```
Splash → DeviceSelection → DeviceMain → DeviceSettings/DeviceControl
```

### 2. Device Abstraction Layer

**Files**: `devices/device_base.hpp/cpp`, `devices/device_registry.hpp/cpp`

- **Purpose**: Abstract interface for device implementations
- **Architecture**: Base class with virtual methods
- **Features**:
  - Device-specific rendering
  - Protocol event handling
  - Settings menu building
  - Connection status tracking

**Base Interface**:
```cpp
class DeviceBase {
    virtual void RenderMainScreen() = 0;
    virtual void HandleButton(ButtonId) = 0;
    virtual void HandleEncoder(EC11Encoder::Direction) = 0;
    virtual void UpdateFromProtocol(const espnow::ProtoEvent&) = 0;
    virtual void BuildSettingsMenu(MenuBuilder&) = 0;
    // ...
};
```

### 3. Device Registry

**Files**: `devices/device_registry.hpp/cpp`

- **Purpose**: Factory for creating device instances
- **Features**:
  - Device ID to class mapping
  - Device name lookup
  - Available device list

**Supported Devices**:
- **Fatigue Tester** (ID: 1)
- **Mock Device** (ID: 2)

### 4. Menu System

**Files**: `menu/menu_items.hpp/cpp`, `menu/menu_system.hpp/cpp`

- **Purpose**: Dynamic menu building and navigation
- **Features**:
  - Device-driven menu construction
  - Value editing (numeric)
  - Choice editing (boolean)
  - Action items
  - Submenu support

**Menu Item Types**:
- `ValueMenuItem`: Numeric value editing
- `ChoiceMenuItem`: Boolean choice editing
- `ActionMenuItem`: Action execution
- `SubMenuItem`: Nested menu support

### 5. ESP-NOW Protocol

**Files**: `protocol/espnow_protocol.hpp/cpp`, `protocol/device_protocols.hpp`

- **Purpose**: Generic ESP-NOW communication
- **Features**:
  - Protocol versioning
  - Device ID routing
  - Generic message types
  - Device-specific payloads

**Protocol Features**:
- Sync byte validation
- CRC16-CCITT error detection
- Sequence ID tracking
- Version negotiation (future)

### 6. Settings Management

**Files**: `settings.hpp/cpp`

- **Purpose**: Persistent settings storage
- **Storage**: NVS (Non-Volatile Storage)
- **Structure**:
  - `TestUnitSettings`: Synced with device
  - `UiSettings`: Local only

### 7. Input Handling

**Files**: `button.hpp/cpp`

- **Purpose**: Button and encoder input
- **Features**:
  - GPIO interrupt handling
  - Debouncing
  - Deep sleep wake support
  - Queue-based event delivery

## State Machine Details

### Splash Screen

- **Purpose**: Initial screen with branding
- **Display**: "ConMed™ Test Devices Remote Control"
- **Footer**: "Press any button to select machine"
- **Transition**: Any button → DeviceSelection

### Device Selection

- **Purpose**: Select device to control
- **Display**: List of available devices
- **Navigation**: Encoder to select, Confirm to enter
- **Auto-select**: If only one device, auto-selects
- **Transition**: Confirm → DeviceMain

### Device Main

- **Purpose**: Device-specific main screen
- **Display**: Device-specific rendering
- **Navigation**:
  - Confirm → DeviceControl
  - Encoder Click → DeviceSettings
  - Back → DeviceSelection

### Device Settings

- **Purpose**: Device configuration menu
- **Display**: Dynamic menu from device
- **Navigation**: Encoder + buttons
- **Transition**: Back → DeviceMain

### Device Control

- **Purpose**: Device control screen (start/pause/stop)
- **Display**: Device-specific control UI
- **Navigation**: Back → DeviceMain

### Popup

- **Purpose**: Confirmation dialogs
- **Display**: Popup overlay
- **Navigation**: Encoder + Confirm button

## Device Development Guide

### Creating a New Device

1. **Create Device Class**:
   ```cpp
   class MyDevice : public DeviceBase {
   public:
       MyDevice(Adafruit_SH1106* display, Settings* settings) noexcept;
       
       uint8_t GetDeviceId() const noexcept override;
       const char* GetDeviceName() const noexcept override;
       void RenderMainScreen() noexcept override;
       // ... implement all virtual methods
   };
   ```

2. **Register Device**:
   - Add device ID to `device_registry.hpp`
   - Add case in `device_registry.cpp::CreateDevice()`
   - Add name mapping in `device_registry.cpp`

3. **Define Protocol**:
   - Add payload structures to `device_protocols.hpp`
   - Implement protocol handling in device class

4. **Build Menu**:
   - Implement `BuildSettingsMenu()` method
   - Use `MenuBuilder` to add menu items

## Protocol Architecture

### Generic Protocol Layer

- **Version**: Protocol version for compatibility
- **Device ID**: Routes messages to correct device
- **Message Types**: Generic types (Config, Command, Status, etc.)

### Device-Specific Layer

- **Payload Structures**: Device-specific data structures
- **Command IDs**: Device-specific command enumeration
- **Status Structures**: Device-specific status data

## Menu System Architecture

### Menu Building

1. **Device Builds Menu**: Device calls `MenuBuilder` methods
2. **Menu Items Created**: Menu items allocated and linked
3. **Menu System Renders**: Menu system displays menu
4. **User Interacts**: Encoder/buttons navigate and edit
5. **Values Updated**: Direct pointer updates to settings

### Menu Item Types

- **ValueMenuItem**: Edits numeric values (uint32_t*)
- **ChoiceMenuItem**: Edits boolean choices (bool*)
- **ActionMenuItem**: Executes callbacks
- **SubMenuItem**: Contains child menu items

## Display System

### OLED Display (SH1106)

- **Resolution**: 128x64 pixels
- **Interface**: I2C
- **Library**: Adafruit_GFX + Adafruit_SH1106

### Rendering

- **Device-Specific**: Each device renders its own screens
- **Menu System**: Menu system renders menus
- **UI Controller**: Coordinates overall display

## Settings Persistence

### NVS Storage

- **Namespace**: `fatigue_rc`
- **Structure**: Binary blob with CRC validation
- **Validation**: CRC32 checksum for integrity

### Settings Structure

```cpp
struct Settings {
    TestUnitSettings test_unit;  // Synced with device
    UiSettings       ui;         // Local only
};
```

## Extension Points

1. **New Device Types**: Implement `DeviceBase` interface
2. **New Menu Items**: Extend `MenuItemBase` class
3. **New Protocol Messages**: Add to `MsgType` enum
4. **New Display Types**: Implement display driver

## Coding Standards

This project follows `docs/CODING_STANDARDS.md`:

- **Public functions**: PascalCase
- **Private functions**: camelCase
- **Member variables**: snake_case + trailing underscore
- **Constants**: UPPER_CASE + trailing underscore
- **Namespaces**: lowercase

