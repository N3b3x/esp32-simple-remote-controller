// Arduino compatibility implementation for ESP-IDF
// This file provides global instances of SPI and Serial

#include "Arduino.h"
#include "SPI.h"
#include "Serial.h"

// Global SPI instance (defined in SPI.cpp)
// Global Serial instance (defined here)
SerialClass Serial;

