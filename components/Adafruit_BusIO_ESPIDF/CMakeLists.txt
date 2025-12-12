# Adafruit BusIO ESP-IDF Native Implementation
# Minimal SPI-only implementation for ThinkInk e-ink displays

cmake_minimum_required(VERSION 3.5)

idf_component_register(
    SRCS "Adafruit_I2CDevice.cpp" "SPI.cpp"
         "Adafruit_SPIDevice.cpp"
         "Wire.cpp"
         "arduino_compat.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_spi esp_driver_i2c esp_driver_gpio freertos esp_timer
)

