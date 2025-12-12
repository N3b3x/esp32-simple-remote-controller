# ESP32 Remote Controller

Standalone remote controller application for ESP32 devices. Supports multiple device types via generic ESP-NOW protocol.

## Overview

This project provides a generic remote controller framework that can control multiple device types through a unified ESP-NOW protocol. The controller features:

- **Device Abstraction Layer**: Base classes for device implementations
- **Dynamic Menu System**: Device-driven menu building
- **Generic ESP-NOW Protocol**: Supports multiple device types with versioning
- **OLED Display**: 1.3" SH1106 OLED display with rotary encoder navigation
- **Device Selection**: Automatic or manual device selection
- **Settings Persistence**: NVS-based settings storage

## Project Structure

```
examples/esp32_remote_controller/
├── main/
│   ├── main.cpp                    # Application entry point
│   ├── config.hpp                  # Hardware configuration
│   ├── button.hpp/cpp              # Button handling
│   ├── settings.hpp/cpp            # Settings storage
│   ├── protocol/
│   │   ├── espnow_protocol.hpp/cpp # Generic ESP-NOW protocol
│   │   └── device_protocols.hpp    # Device-specific protocol definitions
│   ├── devices/
│   │   ├── device_base.hpp/cpp     # Base device interface
│   │   ├── device_registry.hpp/cpp # Device registry and factory
│   │   ├── fatigue_tester.hpp/cpp  # Fatigue test device implementation
│   │   └── mock_device.hpp/cpp     # Mock device for demonstration
│   ├── menu/
│   │   ├── menu_items.hpp/cpp      # Menu item classes
│   │   └── menu_system.hpp/cpp     # Dynamic menu system builder
│   └── ui/
│       ├── ui_state.hpp             # UI state definitions
│       └── ui_controller.hpp/cpp    # Main UI controller
├── components/                     # Display drivers and encoder
├── scripts/                       # Build scripts
├── CMakeLists.txt
├── app_config.yml
└── README.md
```

## Hardware Requirements

- **Display**: 1.3" SH1106 OLED (128x64, I2C)
- **Input**: EC11 Rotary Encoder
- **Buttons**: BACK and CONFIRM physical buttons
- **Target**: ESP32-C6

## Building

Use the build script:

```bash
cd examples/esp32_remote_controller
./scripts/build_app.sh remote_controller Release
```

## Coding Standards

This project follows the coding standards defined in `docs/CODING_STANDARDS.md`:

- **Public functions**: PascalCase (`Init()`, `RenderMainScreen()`)
- **Private functions**: camelCase (`buildMenuStructure()`, `renderMenu()`)
- **Member variables**: snake_case + trailing underscore (`display_`, `current_state_`)
- **Constants**: UPPER_CASE + trailing underscore (`MAX_DEVICES_`, `DEFAULT_TIMEOUT_MS_`)
- **Namespaces**: lowercase for acronyms (`espnow`, `device_registry`)

## Protocol

The ESP-NOW protocol includes:
- Protocol versioning for future compatibility
- Device ID routing for multi-device support
- Generic message types (Config, Command, Status, etc.)
- Device-specific payload structures

## Device Support

Currently supported devices:
- **Fatigue Tester** (Device ID: 1)
- **Mock Device** (Device ID: 2)

New devices can be added by:
1. Creating a new device class inheriting from `DeviceBase`
2. Implementing device-specific screens and protocol handling
3. Registering the device in `device_registry`

## Documentation

Comprehensive documentation is available:

- **[ARCHITECTURE.md](ARCHITECTURE.md)** - System architecture, component overview, and design patterns
- **[PROTOCOL.md](PROTOCOL.md)** - Complete ESP-NOW protocol specification
- **[DEVICE_DEVELOPMENT.md](DEVICE_DEVELOPMENT.md)** - Guide for adding new device types
- **[HARDWARE_SETUP.md](HARDWARE_SETUP.md)** - Hardware setup guide, pin configuration, and wiring diagrams

## Dependencies

- ESP-IDF v5.5
- Adafruit display libraries (GFX, SH1106, BusIO)
- EC11 Encoder component
- No TMC driver dependencies (standalone project)

## License

See parent repository for license information.
