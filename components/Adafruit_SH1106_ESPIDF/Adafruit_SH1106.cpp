/**
 * @file Adafruit_SH1106.cpp
 * @brief ESP-IDF native implementation for SH1106 OLED displays
 */

#include "Adafruit_SH1106.h"
#include "../Adafruit_BusIO_ESPIDF/Arduino.h"
#include "esp_log.h"
#include <cstdlib>
#include <cstring>

static const char* TAG_SH1106 = "SH1106";

// SH1106 initialization sequence
static const uint8_t sh1106_init_sequence[] = {
    SH1106_DISPLAYOFF,                    // 0xAE
    SH1106_SETDISPLAYCLOCKDIV, 0x80,     // 0xD5, 0x80
    SH1106_SETMULTIPLEX, 0x3F,           // 0xA8, 0x3F (64-1)
    SH1106_SETDISPLAYOFFSET, 0x00,       // 0xD3, 0x00
    SH1106_SETSTARTLINE | 0x0,           // 0x40 | line#
    SH1106_CHARGEPUMP, 0x14,             // 0x8D, 0x14 (enable charge pump)
    SH1106_MEMORYMODE, 0x00,             // 0x20, 0x00 (horizontal addressing)
    SH1106_SEGREMAP | 0x1,               // 0xA0 | remap
    SH1106_COMSCANDEC,                   // 0xC8
    SH1106_SETCOMPINS, 0x12,             // 0xDA, 0x12 (128x64)
    SH1106_SETCONTRAST, 0xCF,            // 0x81, 0xCF
    SH1106_SETPRECHARGE, 0xF1,           // 0xD9, 0xF1
    SH1106_SETVCOMDETECT, 0x40,          // 0xDB, 0x40
    SH1106_DISPLAYALLON_RESUME,          // 0xA4
    SH1106_NORMALDISPLAY,                // 0xA6
    SH1106_DISPLAYON                     // 0xAF
};

Adafruit_SH1106::Adafruit_SH1106(uint16_t w, uint16_t h, TwoWire *twi, 
                                 int8_t rst_pin, uint8_t i2caddr)
    : Adafruit_GFX(w, h), i2c_dev(nullptr), buffer(nullptr), 
      rstpin(rst_pin), i2caddr(i2caddr), vccstate(SH1106_SWITCHCAPVCC) {
    // Note: TwoWire* is not used in ESP-IDF implementation, but kept for compatibility
    (void)twi;
}

Adafruit_SH1106::~Adafruit_SH1106() {
    if (buffer) {
        free(buffer);
        buffer = nullptr;
    }
    if (i2c_dev) {
        delete i2c_dev;
        i2c_dev = nullptr;
    }
}

bool Adafruit_SH1106::begin(uint8_t i2caddr, bool reset) {
    // Check if I2C address changed and recreate device if needed
    bool address_changed = (i2c_dev != nullptr && this->i2caddr != i2caddr);
    
    // Update member variable with the provided address
    this->i2caddr = i2caddr;
    
    // Allocate display buffer (128x64 = 1024 bytes, but SH1106 uses 128x64 = 8 pages * 128 bytes)
    if (!buffer) {
        buffer = (uint8_t*)malloc((WIDTH * HEIGHT) / 8);
        if (!buffer) {
            ESP_LOGE(TAG_SH1106, "Failed to allocate display buffer");
            return false;
        }
        memset(buffer, 0, (WIDTH * HEIGHT) / 8);
    }
    
    // Create or recreate I2C device if needed or address changed
    if (!i2c_dev || address_changed) {
        if (i2c_dev) {
            delete i2c_dev;
            i2c_dev = nullptr;
        }
        i2c_dev = new Adafruit_I2CDevice(i2caddr);
        if (!i2c_dev) {
            ESP_LOGE(TAG_SH1106, "Failed to create I2C device");
            return false;
        }
    }
    
    // Initialize I2C device
    if (!i2c_dev->begin()) {
        ESP_LOGE(TAG_SH1106, "Failed to initialize I2C device");
        return false;
    }
    
    // Hardware reset if pin is specified
    if (reset && rstpin >= 0) {
        pinMode(rstpin, OUTPUT);
        digitalWrite(rstpin, HIGH);
        delay(10);
        digitalWrite(rstpin, LOW);
        delay(10);
        digitalWrite(rstpin, HIGH);
        delay(10);
    }
    
    // Initialize display
    if (!initDisplay()) {
        ESP_LOGE(TAG_SH1106, "Failed to initialize display");
        return false;
    }
    
    ESP_LOGI(TAG_SH1106, "SH1106 initialized: %dx%d, I2C addr=0x%02X", 
             WIDTH, HEIGHT, this->i2caddr);
    return true;
}

bool Adafruit_SH1106::initDisplay(void) {
    // Send initialization sequence
    if (!sh1106_commandList(sh1106_init_sequence, sizeof(sh1106_init_sequence))) {
        return false;
    }
    
    // Clear display
    clearDisplay();
    display();
    
    return true;
}

void Adafruit_SH1106::clearDisplay(void) {
    if (buffer) {
        memset(buffer, 0, (WIDTH * HEIGHT) / 8);
    }
}

void Adafruit_SH1106::invertDisplay(bool i) {
    sh1106_command(i ? SH1106_INVERTDISPLAY : SH1106_NORMALDISPLAY);
}

void Adafruit_SH1106::dim(bool dim) {
    uint8_t contrast = dim ? 0 : 0xCF;
    sh1106_command(SH1106_SETCONTRAST);
    sh1106_command(contrast);
}

void Adafruit_SH1106::display(void) {
    if (!buffer || !i2c_dev) {
        return;
    }
    
    // SH1106 uses page addressing mode
    // Each page is 8 pixels tall, 128 pixels wide
    // There are 8 pages (64 pixels / 8 = 8 pages)
    for (uint8_t page = 0; page < 8; page++) {
        // SH1106 requires setting the page address and column address for each page
        // Unlike SSD1306, it doesn't support automatic page increment or the 0x21/0x22 commands
        
        // 1. Set Page Address (0xB0 - 0xB7)
        sh1106_command(0xB0 + page);
        
        // 2. Set Column Address (offset by 0 for 128 pixel wide displays left-aligned in 132 RAM)
        // Lower 4 bits of column start address (0x00 - 0x0F) -> 0 & 0x0F = 0x00
        sh1106_command(0x00 | 0x00); 
        // Higher 4 bits of column start address (0x10 - 0x1F) -> 0 >> 4 = 0x00 -> 0x10
        sh1106_command(0x10 | 0x00);
        
        // Send page data with data mode prefix (0x40)
        uint8_t *page_buffer = buffer + (page * WIDTH);
        uint8_t data_prefix = 0x40; // Data mode
        i2c_dev->write(page_buffer, WIDTH, true, &data_prefix, 1);
    }
}

void Adafruit_SH1106::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (!buffer) {
        return;
    }

    // Transform coordinates based on rotation
    int16_t t;
    switch (rotation) {
        case 1:
            t = x;
            x = y;
            y = HEIGHT - 1 - t;
            break;
        case 2:
            x = WIDTH - 1 - x;
            y = HEIGHT - 1 - y;
            break;
        case 3:
            t = x;
            x = WIDTH - 1 - y;
            y = t;
            break;
    }

    // Check bounds (using physical dimensions)
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) {
        return;
    }
    
    // SH1106 uses page addressing
    // Each page is 8 pixels tall
    uint8_t page = y / 8;
    uint8_t bit = y % 8;
    uint16_t index = page * WIDTH + x;
    
    if (color) {
        buffer[index] |= (1 << bit);
    } else {
        buffer[index] &= ~(1 << bit);
    }
}

bool Adafruit_SH1106::getPixel(int16_t x, int16_t y) {
    if (!buffer) {
        return false;
    }

    // Transform coordinates based on rotation
    int16_t t;
    switch (rotation) {
        case 1:
            t = x;
            x = y;
            y = HEIGHT - 1 - t;
            break;
        case 2:
            x = WIDTH - 1 - x;
            y = HEIGHT - 1 - y;
            break;
        case 3:
            t = x;
            x = WIDTH - 1 - y;
            y = t;
            break;
    }

    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) {
        return false;
    }
    
    uint8_t page = y / 8;
    uint8_t bit = y % 8;
    uint16_t index = page * WIDTH + x;
    
    return (buffer[index] >> bit) & 1;
}

void Adafruit_SH1106::sh1106_command(uint8_t c) {
    if (!i2c_dev) {
        return;
    }
    
    // I2C command: send 0x00 (command mode) followed by command byte
    uint8_t cmd[2] = {0x00, c};
    i2c_dev->write(cmd, 2);
}

bool Adafruit_SH1106::sh1106_commandList(const uint8_t *c, uint8_t n) {
    if (!i2c_dev || !c) {
        return false;
    }
    
    // Send commands one by one
    for (uint8_t i = 0; i < n; i++) {
        sh1106_command(c[i]);
        delay(1); // Small delay between commands
    }
    
    return true;
}

// Scrolling functions (stubs for now)
void Adafruit_SH1106::startscrollright(uint8_t start, uint8_t stop) {
    sh1106_command(SH1106_RIGHT_HORIZONTAL_SCROLL);
    sh1106_command(0x00);
    sh1106_command(start);
    sh1106_command(0x00);
    sh1106_command(stop);
    sh1106_command(0x00);
    sh1106_command(0xFF);
    sh1106_command(SH1106_ACTIVATE_SCROLL);
}

void Adafruit_SH1106::startscrollleft(uint8_t start, uint8_t stop) {
    sh1106_command(SH1106_LEFT_HORIZONTAL_SCROLL);
    sh1106_command(0x00);
    sh1106_command(start);
    sh1106_command(0x00);
    sh1106_command(stop);
    sh1106_command(0x00);
    sh1106_command(0xFF);
    sh1106_command(SH1106_ACTIVATE_SCROLL);
}

void Adafruit_SH1106::startscrolldiagright(uint8_t start, uint8_t stop) {
    sh1106_command(SH1106_SET_VERTICAL_SCROLL_AREA);
    sh1106_command(0x00);
    sh1106_command(HEIGHT);
    sh1106_command(SH1106_VERTICAL_AND_RIGHT_HORIZONTAL_SCROLL);
    sh1106_command(0x00);
    sh1106_command(start);
    sh1106_command(0x00);
    sh1106_command(stop);
    sh1106_command(0x01);
    sh1106_command(SH1106_ACTIVATE_SCROLL);
}

void Adafruit_SH1106::startscrolldiagleft(uint8_t start, uint8_t stop) {
    sh1106_command(SH1106_SET_VERTICAL_SCROLL_AREA);
    sh1106_command(0x00);
    sh1106_command(HEIGHT);
    sh1106_command(SH1106_VERTICAL_AND_LEFT_HORIZONTAL_SCROLL);
    sh1106_command(0x00);
    sh1106_command(start);
    sh1106_command(0x00);
    sh1106_command(stop);
    sh1106_command(0x01);
    sh1106_command(SH1106_ACTIVATE_SCROLL);
}

void Adafruit_SH1106::stopscroll(void) {
    sh1106_command(SH1106_DEACTIVATE_SCROLL);
}

