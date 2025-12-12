#ifndef Adafruit_SPIDevice_h
#define Adafruit_SPIDevice_h

#include <Arduino.h>
#include <SPI.h>

// Bit order enum matching Adafruit library
typedef enum {
    SPI_BITORDER_MSBFIRST = MSBFIRST,
    SPI_BITORDER_LSBFIRST = LSBFIRST,
} BusIOBitOrder;

// The class which defines how we will talk to this device over SPI
class Adafruit_SPIDevice {
public:
    // Constructor for hardware SPI
    // Uses SPIClass* to get bus configuration (pins, host) - bus must be initialized via SPIClass::begin()
    Adafruit_SPIDevice(int8_t cspin, uint32_t freq = 1000000,
                       BusIOBitOrder dataOrder = SPI_BITORDER_MSBFIRST,
                       uint8_t dataMode = SPI_MODE0, SPIClass *theSPI = &SPI);
    
    // Constructor for software SPI (for compatibility, but not implemented)
    Adafruit_SPIDevice(int8_t cspin, int8_t sck, int8_t miso, int8_t mosi,
                       uint32_t freq = 1000000,
                       BusIOBitOrder dataOrder = SPI_BITORDER_MSBFIRST,
                       uint8_t dataMode = SPI_MODE0);
    
    ~Adafruit_SPIDevice();

    bool begin(void);
    uint8_t transfer(uint8_t send);
    void beginTransaction(void);
    void endTransaction(void);
    void beginTransactionWithAssertingCS();
    void endTransactionWithDeassertingCS();
    size_t write(uint8_t data);
    size_t write(const uint8_t* buffer, size_t len);

private:
    SPIClass *_spi;
    uint32_t _freq;
    BusIOBitOrder _dataOrder;
    uint8_t _dataMode;
    void setChipSelect(int value);

    int8_t _cs, _sck, _mosi, _miso;
    bool _begun;
    spi_device_handle_t spi_device_;
    spi_host_device_t spi_host_;
};

#endif // Adafruit_SPIDevice_h

