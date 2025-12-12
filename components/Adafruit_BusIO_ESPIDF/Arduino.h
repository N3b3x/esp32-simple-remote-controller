// Minimal Arduino.h compatibility for ESP-IDF
// Provides only essential types and functions needed for Adafruit libraries
// NO Wire, String, Stream, or other Arduino classes

#ifndef ARDUINO_H
#define ARDUINO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

// Arduino version defines
#define ARDUINO 100
#define ARDUINO_ARCH_ESP32 1
#define ESP32 1

// Basic Arduino types
typedef uint8_t byte;
typedef bool boolean;

// Pin modes
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3

// Digital I/O
#define LOW 0
#define HIGH 1

// Bit order
#define MSBFIRST 1
#define LSBFIRST 0

// Print base constants
#define HEX 16
#define DEC 10
#define OCT 8
#define BIN 2

// Include ESP-IDF GPIO for actual pin control
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"

// GPIO functions (accept Arduino pin numbers as int8_t)
static inline void pinMode(int8_t pin, int mode) {
    if (pin < 0) return; // Invalid pin
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << pin);
    cfg.mode = (mode == OUTPUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
    cfg.pull_up_en = (mode == INPUT_PULLUP) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = (mode == INPUT_PULLDOWN) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

static inline void digitalWrite(int8_t pin, uint32_t level) {
    if (pin < 0) return; // Invalid pin
    gpio_set_level(static_cast<gpio_num_t>(pin), level);
}

static inline int digitalRead(int8_t pin) {
    if (pin < 0) return 0; // Invalid pin
    return gpio_get_level(static_cast<gpio_num_t>(pin));
}

// Timing functions
static inline void delay(unsigned long ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static inline void delayMicroseconds(unsigned long us) {
    ets_delay_us(us);
}

static inline unsigned long millis() {
    return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

static inline unsigned long micros() {
    return (unsigned long)esp_timer_get_time();
}

// AVR-specific macros (stubs for compatibility with MCPSRAM)
#define digitalPinToPort(pin) (0)
#define portOutputRegister(port) ((volatile uint8_t*)nullptr)
#define portInputRegister(port) ((volatile uint8_t*)nullptr)
#define digitalPinToBitMask(pin) (1 << (pin))

// min/max functions (for compatibility, using inline to avoid macro conflicts)
// Note: These are only defined if not already defined by standard library
// Use std::min/std::max in C++ code when possible
#ifdef __cplusplus
#include <algorithm>
using std::min;
using std::max;
#else
// C compatibility - use macros only if not conflicting
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif
#endif

// Forward declarations
class SPIClass;
struct SPISettings;

// F() macro for string literals (no-op in ESP-IDF, just returns pointer)
#define F(str) (str)

// __FlashStringHelper stub (for compatibility)
class __FlashStringHelper {};

// String type (minimal stub - use std::string in actual code)
#include <string>
typedef std::string String;

// Include Serial definition
#include "Serial.h"

#endif // ARDUINO_H

