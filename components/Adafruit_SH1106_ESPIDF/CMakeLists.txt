# Adafruit SH1106 OLED Driver for ESP-IDF
# Native ESP-IDF I2C implementation for 1.3" SH1106 OLED displays
# Compatible with Adafruit GFX library

cmake_minimum_required(VERSION 3.5)

idf_component_register(
    SRCS "Adafruit_SH1106.cpp"
    INCLUDE_DIRS "." "../Adafruit_GFX" "../Adafruit_BusIO_ESPIDF"
    REQUIRES driver esp_driver_i2c esp_driver_gpio freertos Adafruit_GFX Adafruit_BusIO_ESPIDF
)

