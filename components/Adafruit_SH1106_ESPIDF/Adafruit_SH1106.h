/**
 * @file Adafruit_SH1106.h
 * @brief ESP-IDF native driver for 1.3" SH1106 OLED displays (128x64)
 * 
 * Compatible with Adafruit GFX library. Uses ESP-IDF I2C master driver.
 */

#pragma once

#include "../Adafruit_GFX/Adafruit_GFX.h"
#include "../Adafruit_BusIO_ESPIDF/Adafruit_I2CDevice.h"
#include "../Adafruit_BusIO_ESPIDF/Wire.h"
#include <cstdint>

#define SH1106_SWITCHCAPVCC 0x02
#define SH1106_I2C_ADDRESS 0x3C // or 0x3D

// SH1106 commands
#define SH1106_SETCONTRAST 0x81
#define SH1106_DISPLAYALLON_RESUME 0xA4
#define SH1106_DISPLAYALLON 0xA5
#define SH1106_NORMALDISPLAY 0xA6
#define SH1106_INVERTDISPLAY 0xA7
#define SH1106_DISPLAYOFF 0xAE
#define SH1106_DISPLAYON 0xAF
#define SH1106_SETDISPLAYOFFSET 0xD3
#define SH1106_SETCOMPINS 0xDA
#define SH1106_SETVCOMDETECT 0xDB
#define SH1106_SETDISPLAYCLOCKDIV 0xD5
#define SH1106_SETPRECHARGE 0xD9
#define SH1106_SETMULTIPLEX 0xA8
#define SH1106_SETLOWCOLUMN 0x00
#define SH1106_SETHIGHCOLUMN 0x10
#define SH1106_SETSTARTLINE 0x40
#define SH1106_MEMORYMODE 0x20
#define SH1106_COLUMNADDR 0x21
#define SH1106_PAGEADDR 0x22
#define SH1106_COMSCANINC 0xC0
#define SH1106_COMSCANDEC 0xC8
#define SH1106_SEGREMAP 0xA0
#define SH1106_CHARGEPUMP 0x8D
#define SH1106_EXTERNALVCC 0x1
#define SH1106_SWITCHCAPVCC 0x2

// Scrolling commands
#define SH1106_ACTIVATE_SCROLL 0x2F
#define SH1106_DEACTIVATE_SCROLL 0x2E
#define SH1106_SET_VERTICAL_SCROLL_AREA 0xA3
#define SH1106_RIGHT_HORIZONTAL_SCROLL 0x26
#define SH1106_LEFT_HORIZONTAL_SCROLL 0x27
#define SH1106_VERTICAL_AND_RIGHT_HORIZONTAL_SCROLL 0x29
#define SH1106_VERTICAL_AND_LEFT_HORIZONTAL_SCROLL 0x2A

/**
 * @brief Driver for SH1106 OLED displays (128x64)
 */
class Adafruit_SH1106 : public Adafruit_GFX {
public:
    /**
     * @brief Constructor for I2C interface
     * @param w Display width (typically 128)
     * @param h Display height (typically 64)
     * @param twi I2C interface (TwoWire*), defaults to &Wire
     * @param rst_pin Reset pin (-1 if not used)
     * @param i2caddr I2C address (0x3C or 0x3D)
     */
    Adafruit_SH1106(uint16_t w, uint16_t h, TwoWire *twi = &Wire, 
                    int8_t rst_pin = -1, uint8_t i2caddr = SH1106_I2C_ADDRESS);
    
    /**
     * @brief Destructor
     */
    ~Adafruit_SH1106();
    
    /**
     * @brief Initialize the display
     * @param i2caddr I2C address (0x3C or 0x3D)
     * @param reset If true, perform hardware reset
     * @return true if successful, false otherwise
     */
    bool begin(uint8_t i2caddr = SH1106_I2C_ADDRESS, bool reset = true);
    
    /**
     * @brief Clear the display buffer
     */
    void clearDisplay(void);
    
    /**
     * @brief Invert display colors
     * @param i If true, invert display
     */
    void invertDisplay(bool i);
    
    /**
     * @brief Set display contrast
     * @param contrast Contrast value (0-255)
     */
    void dim(bool dim);
    
    /**
     * @brief Display the buffer on screen
     */
    void display(void);
    
    /**
     * @brief Start scrolling display
     */
    void startscrollright(uint8_t start, uint8_t stop);
    void startscrollleft(uint8_t start, uint8_t stop);
    void startscrolldiagright(uint8_t start, uint8_t stop);
    void startscrolldiagleft(uint8_t start, uint8_t stop);
    void stopscroll(void);
    
    /**
     * @brief Draw a pixel (required by Adafruit_GFX)
     * @param x X coordinate
     * @param y Y coordinate
     * @param color Pixel color (0=black, 1=white)
     */
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    
    /**
     * @brief Get pixel value from buffer
     * @param x X coordinate
     * @param y Y coordinate
     * @return true if pixel is set, false otherwise
     */
    bool getPixel(int16_t x, int16_t y);
    
    /**
     * @brief Get display buffer pointer
     * @return Pointer to display buffer
     */
    uint8_t *getBuffer(void) { return buffer; }

private:
    Adafruit_I2CDevice *i2c_dev;
    uint8_t *buffer;
    int8_t rstpin;
    uint8_t i2caddr;
    bool vccstate;
    
    /**
     * @brief Send command to display
     * @param c Command byte
     */
    void sh1106_command(uint8_t c);
    
    /**
     * @brief Send command list to display
     * @param c Pointer to command array
     * @param n Number of commands
     * @return true if successful
     */
    bool sh1106_commandList(const uint8_t *c, uint8_t n);
    
    /**
     * @brief Initialize display hardware
     * @return true if successful
     */
    bool initDisplay(void);
};

