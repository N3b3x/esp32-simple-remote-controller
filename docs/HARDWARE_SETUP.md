# Hardware Setup Guide - ESP32 Remote Controller

## Overview

This guide covers the hardware setup for the ESP32 Remote Controller, including OLED display connections, encoder setup, and button wiring.

## Hardware Requirements

### Required Components

1. **ESP32 Development Board** (ESP32-C6 recommended)
2. **1.3" SH1106 OLED Display** (I2C interface)
3. **EC11 Rotary Encoder Module**
4. **Two Physical Buttons** (BACK and CONFIRM)
5. **Power Supply** (for ESP32)

## Pin Configuration

### OLED Display (I2C)

Edit `main/config.hpp` to match your wiring:

```cpp
// I2C bus configuration
static constexpr i2c_port_t OLED_I2C_PORT_ = I2C_NUM_0;
static constexpr gpio_num_t OLED_SDA_PIN_  = GPIO_NUM_5;   // Data line
static constexpr gpio_num_t OLED_SCL_PIN_  = GPIO_NUM_23;  // Clock line
static constexpr uint32_t   OLED_I2C_FREQ_ = 400000;       // 400kHz
static constexpr uint8_t    OLED_I2C_ADDR_ = 0x3C;        // I2C address
```

**Note**: SH1106 typically uses 0x3C, but some modules use 0x3D.

### EC11 Rotary Encoder

```cpp
// Encoder pins
static constexpr gpio_num_t ENCODER_TRA_PIN_ = GPIO_NUM_21;  // Phase A (CLK)
static constexpr gpio_num_t ENCODER_TRB_PIN_ = GPIO_NUM_7;   // Phase B (DT)
static constexpr gpio_num_t ENCODER_PSH_PIN_ = GPIO_NUM_22;  // Push button (SW)
```

**Configuration**:
- **Pulses per Revolution**: 20 (configurable)
- **Debounce**: 50ms rotation, 100ms button

### Physical Buttons

```cpp
// Physical buttons
static constexpr gpio_num_t BTN_BACK_GPIO_    = GPIO_NUM_6;   // BACK button
static constexpr gpio_num_t BTN_CONFIRM_GPIO_  = GPIO_NUM_4;   // CONFIRM button
```

**Configuration**:
- **Type**: Active-low with pull-up
- **Debounce**: 50ms

## Wiring Diagram

### ESP32-C6 Beetle Board Reference

```
ESP32-C6                    OLED Display (SH1106)
┌─────────┐                 ┌──────────────┐
│ GPIO 5  │─────────────────│ SDA          │
│ GPIO 23 │─────────────────│ SCL          │
│ 3.3V    │─────────────────│ VCC          │
│ GND     │─────────────────│ GND          │
└─────────┘                 └──────────────┘

ESP32-C6                    EC11 Encoder
┌─────────┐                 ┌──────────────┐
│ GPIO 21 │─────────────────│ TRA (CLK)    │
│ GPIO 7  │─────────────────│ TRB (DT)      │
│ GPIO 22 │─────────────────│ PSH (SW)     │
│ 3.3V    │─────────────────│ VCC          │
│ GND     │─────────────────│ GND          │
└─────────┘                 └──────────────┘

ESP32-C6                    Buttons
┌─────────┐                 ┌──────────────┐
│ GPIO 4  │─────────────────│ CONFIRM      │
│ GPIO 6  │─────────────────│ BACK         │
│ 3.3V    │─────────────────│ (Pull-up)    │
│ GND     │─────────────────│ (Common)     │
└─────────┘                 └──────────────┘
```

## Display Setup

### SH1106 OLED Display

- **Resolution**: 128x64 pixels
- **Interface**: I2C
- **Address**: 0x3C (or 0x3D)
- **Power**: 3.3V or 5V (check module)

### Display Initialization

The display is initialized in `ui_controller.cpp`:

```cpp
Adafruit_SH1106 display(OLED_WIDTH_, OLED_HEIGHT_, &Wire, OLED_RESET_PIN_);
display.begin(OLED_I2C_ADDR_, true);
```

## Encoder Setup

### EC11 Rotary Encoder

- **Type**: Incremental quadrature encoder
- **Pulses per Revolution**: 20 (configurable)
- **Interface**: Quadrature (A/B phases)
- **Push Button**: Integrated switch

### Encoder Wiring

- **TRA (CLK)**: Phase A output → GPIO 21
- **TRB (DT)**: Phase B output → GPIO 7
- **PSH (SW)**: Push button → GPIO 22
- **VCC**: 3.3V or 5V (check module)
- **GND**: Ground

### Encoder Direction

- Clockwise rotation: TRA leads TRB
- Counter-clockwise: TRB leads TRA
- Direction can be inverted in code if needed

## Button Setup

### Button Type

- **Type**: Momentary push buttons
- **Configuration**: Active-low with pull-up
- **Debounce**: 50ms

### Button Functions

- **BACK**: Navigate back, cancel actions
- **CONFIRM**: Confirm selections, critical actions

### Deep Sleep Wake

Buttons must be RTC-capable GPIOs for deep sleep wake:
- Check ESP32-C6 RTC GPIO list
- Configure wake sources in `button.cpp`

## Power Supply

### Requirements

- **Voltage**: 3.3V or 5V (depending on ESP32 board)
- **Current**: ~100-300mA typical
- **Regulation**: Stable voltage

### Power Considerations

- **Display**: Low current (~10-20mA)
- **Encoder**: Very low current
- **Buttons**: Negligible current
- **Deep Sleep**: Very low current (< 10μA)

## Initial Testing

### 1. Serial Connection

Connect to ESP32 via USB:

```bash
screen /dev/ttyUSB0 115200
```

### 2. Verify Initialization

Look for startup messages:
```
[Main] Boot, wakeup cause: 0
[UI] Initializing UI controller...
[ESP-NOW] Initialized
```

### 3. Test Display

- Display should show splash screen
- Check text is readable
- Verify display refresh works

### 4. Test Encoder

- Rotate encoder: Should navigate menus
- Press encoder button: Should enter settings
- Verify direction is correct

### 5. Test Buttons

- **BACK**: Should navigate back
- **CONFIRM**: Should confirm selections

### 6. Test ESP-NOW

1. Power on test unit first
2. Note MAC address from test unit serial output
3. Configure MAC in `config.hpp`
4. Power on remote controller
5. Verify device discovery

## Troubleshooting

### Display Not Working

1. **Check I2C Wiring**:
   - Verify SDA and SCL connections
   - Check for loose connections
   - Verify pull-up resistors (usually on module)

2. **Check I2C Address**:
   - Try 0x3C and 0x3D
   - Check module documentation
   - Use I2C scanner if available

3. **Check Power**:
   - Verify power to display
   - Check voltage level (3.3V or 5V)
   - Verify current capacity

### Encoder Not Working

1. **Check Wiring**:
   - Verify TRA, TRB, PSH connections
   - Check encoder power
   - Verify GPIO numbers

2. **Check Direction**:
   - Encoder may be wired backwards
   - Invert direction in code if needed
   - Check quadrature signals

3. **Check Debounce**:
   - Adjust debounce time if needed
   - Check for bounce in serial output
   - Verify interrupt configuration

### Buttons Not Working

1. **Check Wiring**:
   - Verify GPIO connections
   - Check button type (active-low)
   - Verify pull-up resistors

2. **Check Configuration**:
   - Verify GPIO numbers in config
   - Check debounce settings
   - Verify interrupt configuration

### ESP-NOW Not Working

1. **Check MAC Address**:
   - Verify MAC is correct in config
   - Check both devices are on same WiFi channel
   - Verify MAC format (6 bytes)

2. **Check Range**:
   - Ensure devices are within range
   - Check for interference
   - Verify line-of-sight if possible

3. **Check Serial Output**:
   - Look for ESP-NOW initialization messages
   - Check for error messages
   - Verify device discovery

## Calibration

### Display Calibration

- Display should work out-of-the-box
- No calibration needed for SH1106
- Check contrast if needed (some modules have contrast pot)

### Encoder Calibration

- Encoder should work with default settings
- Adjust pulses per revolution if needed
- Check direction and invert if needed

## Maintenance

1. **Regular Checks**:
   - Display connections
   - Encoder connections
   - Button functionality

2. **Software Updates**:
   - Keep firmware updated
   - Check for configuration changes

3. **Display Maintenance**:
   - OLED displays can burn in (avoid static images)
   - Check for dead pixels
   - Clean display surface if needed

## Safety Considerations

1. **Power Supply**: Use appropriate power supply for ESP32
2. **I2C**: Check voltage levels (3.3V vs 5V)
3. **Wiring**: Ensure proper connections to avoid shorts
4. **Static**: Handle components carefully (static sensitive)

