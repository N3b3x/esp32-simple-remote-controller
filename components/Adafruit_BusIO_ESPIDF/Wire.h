// Minimal Wire.h stub for ESP-IDF
// I2C is not needed for ThinkInk displays (SPI-only)
// This is a minimal stub to satisfy includes in Adafruit_GFX

#ifndef Wire_h
#define Wire_h

#include <Arduino.h>

// Minimal stub class (not implemented, SPI-only implementation)
class TwoWire {
public:
    void begin() {}
    void beginTransmission(uint8_t address) {}
    uint8_t endTransmission(bool stop = true) { return 0; }
    size_t write(uint8_t data) { return 0; }
    size_t write(const uint8_t* data, size_t length) { return 0; }
    uint8_t requestFrom(uint8_t address, size_t quantity, bool stop = true) { return 0; }
    int available() { return 0; }
    int read() { return -1; }
};

// Global instance
extern TwoWire Wire;

#endif // Wire_h

