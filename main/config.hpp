/**
 * @file config.hpp
 * @brief Configuration for OLED UI board (remote controller)
 * 
 * Hardware Configuration:
 * - Display: 1.3" SH1106 OLED Display (I2C)
 *   - Resolution: 128x64 pixels
 *   - Interface: I2C (typically 0x3C or 0x3D address)
 *   - Driver: SH1106
 * 
 * - Input: EC11 Rotary Encoder Module
 *   - TRA (Phase A): Quadrature encoder output A
 *   - TRB (Phase B): Quadrature encoder output B
 *   - PSH (Push button): Encoder integrated switch
 *   - Pulses per revolution: 20
 * 
 * - Physical Buttons:
 *   - BACK: Hardware button for navigation back
 *   - CONFIRM: Hardware button for critical action confirmation
 * 
 * IMPORTANT: Configure the GPIO pin numbers below to match your hardware wiring!
 * 
 * ESP32C6 Beetle Board Pin Reference:
 * - GPIO 4: CONFIRM button
 * - GPIO 5: I2C SDA
 * - GPIO 6: BACK button
 * - GPIO 7: Encoder TRB (Phase B)
 * - GPIO 21: Encoder TRA (Phase A)
 * - GPIO 22: Encoder push button
 * - GPIO 23: I2C SCL
 */

#pragma once

#include <cstdint>
#include "driver/gpio.h"
#include "driver/i2c_master.h"

// ------------- OLED DISPLAY CONFIG (SH1106, I2C) -------------

// I2C bus configuration for OLED display
static constexpr i2c_port_t OLED_I2C_PORT_ = I2C_NUM_0;  // Use I2C0
static constexpr gpio_num_t OLED_SDA_PIN_  = GPIO_NUM_5;   // I2C Data line (SDA)
static constexpr gpio_num_t OLED_SCL_PIN_  = GPIO_NUM_23;  // I2C Clock line (SCL)
static constexpr uint32_t   OLED_I2C_FREQ_ = 400000;      // I2C frequency: 400kHz (fast mode)
static constexpr uint8_t    OLED_I2C_ADDR_ = 0x3C;        // SH1106 I2C address (try 0x3D if 0x3C doesn't work)

// OLED display dimensions
static constexpr uint16_t OLED_WIDTH_  = 128;
static constexpr uint16_t OLED_HEIGHT_ = 64;

// ------------- EC11 ROTARY ENCODER CONFIG -------------

// EC11 encoder pins (must support GPIO interrupts)
static constexpr gpio_num_t ENCODER_TRA_PIN_ = GPIO_NUM_21;  // Phase A (CLK) - quadrature input
static constexpr gpio_num_t ENCODER_TRB_PIN_ = GPIO_NUM_7;   // Phase B (DT) - quadrature input
static constexpr gpio_num_t ENCODER_PSH_PIN_ = GPIO_NUM_22;  // Push button (SW) - encoder click

// Encoder configuration
static constexpr uint8_t ENCODER_PULSES_PER_REV_ = 20;      // 20 pulses per full rotation
static constexpr uint32_t ENCODER_DEBOUNCE_MS_ = 50;         // Debounce time for encoder rotation
static constexpr uint32_t ENCODER_BUTTON_DEBOUNCE_MS_ = 100; // Debounce time for encoder button

// ------------- PHYSICAL BUTTONS CONFIG -------------

// Physical buttons on the OLED module board
// NOTE: Both buttons are RTC-capable for deep sleep wake
static constexpr gpio_num_t BTN_BACK_GPIO_    = GPIO_NUM_6;   // BACK button (navigate back) - RTC-capable
static constexpr gpio_num_t BTN_CONFIRM_GPIO_ = GPIO_NUM_4;   // CONFIRM button (critical actions) - RTC-capable

// Button debounce configuration
static constexpr uint32_t BUTTON_DEBOUNCE_MS_ = 50;           // Debounce time for physical buttons

// ------------- ESPNOW CONFIG -------------

// Placeholder MAC of the test unit (receiver). Fill with real MAC later.
// This should be set to the MAC address of your test unit ESP32
static constexpr uint8_t TEST_UNIT_MAC_[6] = { 0x24, 0x6F, 0x28, 0x00, 0x00, 0x01 };

// ------------- APP LOGIC -------------

// Inactivity timeout before going to deep sleep (seconds)
static constexpr uint32_t INACTIVITY_TIMEOUT_SEC_ = 60;

// Sleep duration threshold - if sleep exceeds this, show splash instead of restoring state (seconds)
static constexpr uint32_t SLEEP_RESET_THRESHOLD_SEC_ = 30 * 60; // 30 minutes

