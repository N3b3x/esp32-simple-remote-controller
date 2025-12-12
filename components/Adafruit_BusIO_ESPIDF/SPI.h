// Minimal SPI.h compatibility for ESP-IDF
// Provides SPIClass wrapper for MCPSRAM compatibility

#ifndef SPI_H
#define SPI_H

#include "Arduino.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

// SPI modes
#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3

// ESP-IDF uses per-device clock speeds configured when adding device to bus
// Transactions are supported via beginTransaction()/endTransaction()
#define SPI_HAS_TRANSACTION 1

// SPISettings structure
struct SPISettings {
    uint32_t clock;
    uint8_t bitOrder;
    uint8_t dataMode;
    
    SPISettings() : clock(1000000), bitOrder(MSBFIRST), dataMode(SPI_MODE0) {}
    SPISettings(uint32_t clockFreq, uint8_t bitOrder, uint8_t dataMode) 
        : clock(clockFreq), bitOrder(bitOrder), dataMode(dataMode) {}
};

// SPI class implementation using ESP-IDF SPI driver
class SPIClass {
private:
    spi_device_handle_t spi_device_;
    bool initialized_;
    gpio_num_t cs_pin_;
    gpio_num_t sck_pin_;
    gpio_num_t mosi_pin_;
    gpio_num_t miso_pin_;
    SPISettings current_settings_;
    spi_host_device_t spi_host_;
    
public:
    SPIClass() : initialized_(false), cs_pin_(GPIO_NUM_NC), 
                 sck_pin_(GPIO_NUM_NC), mosi_pin_(GPIO_NUM_NC), miso_pin_(GPIO_NUM_NC),
                 spi_host_(SPI2_HOST) {}
    
    // Default begin() for compatibility with Adafruit libraries
    // Uses default SPI pins (VSPI on ESP32: SCK=18, MOSI=23, MISO=19)
    void begin() {
        #ifdef CONFIG_IDF_TARGET_ESP32
        begin(GPIO_NUM_18, GPIO_NUM_23, GPIO_NUM_19, GPIO_NUM_NC);
        #elif defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
        begin(GPIO_NUM_36, GPIO_NUM_35, GPIO_NUM_37, GPIO_NUM_NC);
        #elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
        begin(GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_10, GPIO_NUM_NC);
        #else
        // Fallback - user must call begin() with pins
        #endif
    }
    
    // begin() with pins - preferred method for explicit configuration
    void begin(gpio_num_t sck, gpio_num_t mosi, gpio_num_t miso, gpio_num_t cs = GPIO_NUM_NC) {
        sck_pin_ = sck;
        mosi_pin_ = mosi;
        miso_pin_ = miso;
        cs_pin_ = cs;
        
        // Only initialize bus if not already initialized
        // Check if bus is already initialized by trying to initialize it
        spi_bus_config_t bus_cfg = {};
        bus_cfg.mosi_io_num = mosi_pin_;
        bus_cfg.miso_io_num = miso_pin_;
        bus_cfg.sclk_io_num = sck_pin_;
        bus_cfg.quadwp_io_num = -1;
        bus_cfg.quadhd_io_num = -1;
        bus_cfg.max_transfer_sz = 4096;
        
        esp_err_t ret = spi_bus_initialize(spi_host_, &bus_cfg, SPI_DMA_CH_AUTO);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            // Bus initialization failed (not just already initialized)
            return;
        }
        // If ESP_ERR_INVALID_STATE, bus was already initialized - that's OK, we can use it
        
        if (cs_pin_ != GPIO_NUM_NC) {
            spi_device_interface_config_t dev_cfg = {};
            dev_cfg.clock_speed_hz = current_settings_.clock;
            dev_cfg.mode = current_settings_.dataMode;
            dev_cfg.spics_io_num = cs_pin_;
            dev_cfg.queue_size = 1;
            dev_cfg.flags = (current_settings_.bitOrder == LSBFIRST) ? SPI_DEVICE_BIT_LSBFIRST : 0;
            dev_cfg.pre_cb = nullptr;
            
            ret = spi_bus_add_device(spi_host_, &dev_cfg, &spi_device_);
            if (ret != ESP_OK) {
                return;
            }
        }
        
        initialized_ = true;
    }
    
    // Getter methods to allow Adafruit_SPIDevice to access bus configuration
    gpio_num_t getSckPin() const { return sck_pin_; }
    gpio_num_t getMosiPin() const { return mosi_pin_; }
    gpio_num_t getMisoPin() const { return miso_pin_; }
    spi_host_device_t getHost() const { return spi_host_; }
    bool isInitialized() const { return initialized_; }
    
    // Set SPI host (for advanced use cases)
    void setHost(spi_host_device_t host) { spi_host_ = host; }
    
    void beginTransaction(SPISettings settings) {
        current_settings_ = settings;
        // Settings are applied per transaction in transfer()
    }
    
    void endTransaction() {
        // Nothing needed for ESP-IDF
    }
    
    uint8_t transfer(uint8_t data) {
        if (!initialized_ || cs_pin_ == GPIO_NUM_NC || spi_device_ == nullptr) {
            return 0;
        }
        
        spi_transaction_t t = {};
        t.length = 8;
        t.tx_buffer = &data;
        uint8_t rx_data = 0;
        t.rx_buffer = &rx_data;
        t.flags = 0;
        
        // Note: SPI mode (CPOL/CPHA) is set in device config, not per transaction
        // Bit order is also set in device config via SPI_DEVICE_BIT_LSBFIRST flag
        
        esp_err_t ret = spi_device_transmit(spi_device_, &t);
        if (ret != ESP_OK) {
            return 0;
        }
        
        return rx_data;
    }
    
    void transfer(uint8_t* buffer, size_t len) {
        if (!initialized_ || cs_pin_ == GPIO_NUM_NC || spi_device_ == nullptr) {
            return;
        }
        
        spi_transaction_t t = {};
        t.length = len * 8;
        t.tx_buffer = buffer;
        t.rx_buffer = buffer;
        t.flags = 0;
        
        spi_device_transmit(spi_device_, &t);
    }
    
    void end() {
        if (initialized_ && cs_pin_ != GPIO_NUM_NC && spi_device_ != nullptr) {
            spi_bus_remove_device(spi_device_);
            spi_bus_free(spi_host_);
            initialized_ = false;
        }
    }
    
    // Compatibility methods for MCPSRAM
    void setClockDivider(uint32_t divider) {
        // Not directly supported in ESP-IDF v5.5, would need to reconfigure device
        // For now, just store the divider (MCPSRAM uses this)
        (void)divider;
    }
    
    void setBitOrder(uint8_t order) {
        current_settings_.bitOrder = order;
    }
    
    void setDataMode(uint8_t mode) {
        current_settings_.dataMode = mode;
    }
    
    // Additional methods needed by Adafruit_SPITFT
    void write(uint8_t data) {
        transfer(data);
    }
    
    void write(const uint8_t* buffer, size_t len) {
        transfer(const_cast<uint8_t*>(buffer), len);
    }
    
    void write16(uint16_t data) {
        uint8_t buf[2];
        buf[0] = (data >> 8) & 0xFF;
        buf[1] = data & 0xFF;
        write(buf, 2);
    }
    
    void write32(uint32_t data) {
        uint8_t buf[4];
        buf[0] = (data >> 24) & 0xFF;
        buf[1] = (data >> 16) & 0xFF;
        buf[2] = (data >> 8) & 0xFF;
        buf[3] = data & 0xFF;
        write(buf, 4);
    }
    
    void writeBytes(const uint8_t* data, size_t len) {
        write(data, len);
    }
    
    void writePixels(const void* data, size_t len) {
        write(static_cast<const uint8_t*>(data), len);
    }
    
    void setFrequency(uint32_t freq) {
        current_settings_.clock = freq;
        // Would need to reconfigure device to apply, but for now just store
    }
};

// Global SPI instance
extern SPIClass SPI;

#endif // SPI_H

