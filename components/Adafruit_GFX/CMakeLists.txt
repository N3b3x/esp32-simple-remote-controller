# Adafruit GFX Library for ESP-IDF
# https://github.com/adafruit/Adafruit-GFX-Library
# BSD License

cmake_minimum_required(VERSION 3.5)

idf_component_register(
    SRCS "Adafruit_GFX.cpp" "Adafruit_GrayOLED.cpp" "Adafruit_SPITFT.cpp" "glcdfont.c"
    INCLUDE_DIRS "." "../Adafruit_BusIO_ESPIDF"
    REQUIRES Adafruit_BusIO_ESPIDF driver esp_timer freertos
)
