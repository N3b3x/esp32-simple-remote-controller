// Serial.h - SerialClass definition for ESP-IDF

#ifndef SERIAL_H
#define SERIAL_H

#include "Print.h"

// Serial stub (minimal implementation for compatibility)
class SerialClass : public Print {
public:
    void begin(unsigned long baud) { (void)baud; }
    void end() {}
    size_t write(uint8_t c) override { return 0; }  // Stub - no actual output
    int available() { return 0; }
    int read() { return -1; }
    void flush() {}
};

// Global Serial instance (defined in arduino_compat.cpp)
extern SerialClass Serial;

#endif // SERIAL_H

