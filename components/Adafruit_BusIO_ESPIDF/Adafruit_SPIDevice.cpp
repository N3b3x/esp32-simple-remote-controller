#include "Adafruit_SPIDevice.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char* TAG = "Adafruit_SPIDevice";

// Constructor for hardware SPI
Adafruit_SPIDevice::Adafruit_SPIDevice(int8_t cspin, uint32_t freq,
                                       BusIOBitOrder dataOrder,
                                       uint8_t dataMode, SPIClass *theSPI)
    : _spi(theSPI), _freq(freq), _dataOrder(dataOrder), _dataMode(dataMode),
      _cs(cspin), _sck(-1), _mosi(-1), _miso(-1), _begun(false),
      spi_device_(nullptr), spi_host_(SPI2_HOST) {
    // Get SPI host from SPIClass if available
    if (_spi != nullptr) {
        spi_host_ = _spi->getHost();
    }
}

// Constructor for software SPI (stub - not implemented)
Adafruit_SPIDevice::Adafruit_SPIDevice(int8_t cspin, int8_t sck, int8_t miso, int8_t mosi,
                                       uint32_t freq,
                                       BusIOBitOrder dataOrder,
                                       uint8_t dataMode)
    : _spi(nullptr), _freq(freq), _dataOrder(dataOrder), _dataMode(dataMode),
      _cs(cspin), _sck(sck), _mosi(mosi), _miso(miso), _begun(false),
      spi_device_(nullptr), spi_host_(SPI2_HOST) {
    // Software SPI not implemented - would need bit-banging
}

Adafruit_SPIDevice::~Adafruit_SPIDevice() {
    if (_begun && spi_device_ != nullptr) {
        spi_bus_remove_device(spi_device_);
    }
}

bool Adafruit_SPIDevice::begin(void) {
    if (_cs < 0) {
        ESP_LOGE(TAG, "Invalid CS pin: %d", _cs);
        return false;
    }
    
    // Configure CS pin
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);
    
    if (_spi == nullptr) {
        // Software SPI not supported
        ESP_LOGE(TAG, "SPIClass pointer is null - software SPI not supported");
        return false;
    }
    
    // CRITICAL: Do NOT initialize the SPI bus here!
    // The SPIClass* passed to constructor should have already initialized the bus via begin()
    // We only add this device to the existing bus.
    
    if (!_spi->isInitialized()) {
        ESP_LOGE(TAG, "SPI bus not initialized! Call SPIClass::begin() before creating Adafruit_SPIDevice");
        return false;
    }
    
    // Get SPI host from SPIClass
    spi_host_ = _spi->getHost();
    
    // Add SPI device to the existing bus
    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.clock_speed_hz = _freq;
    dev_cfg.mode = _dataMode;
    dev_cfg.spics_io_num = static_cast<gpio_num_t>(_cs);
    dev_cfg.queue_size = 1;
    dev_cfg.flags = (_dataOrder == SPI_BITORDER_LSBFIRST) ? SPI_DEVICE_BIT_LSBFIRST : 0;
    dev_cfg.pre_cb = nullptr;
    
    esp_err_t ret = spi_bus_add_device(spi_host_, &dev_cfg, &spi_device_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        return false;
    }
    
    _begun = true;
    return true;
}

uint8_t Adafruit_SPIDevice::transfer(uint8_t send) {
    if (!_begun || spi_device_ == nullptr) {
        return 0;
    }
    
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &send;
    uint8_t rx_data = 0;
    t.rx_buffer = &rx_data;
    t.flags = 0;
    
    esp_err_t ret = spi_device_transmit(spi_device_, &t);
    if (ret != ESP_OK) {
        return 0;
    }
    
    return rx_data;
}

void Adafruit_SPIDevice::beginTransaction(void) {
    // ESP-IDF handles transactions automatically via CS pin
    // This is a no-op, but kept for compatibility
}

void Adafruit_SPIDevice::endTransaction(void) {
    // ESP-IDF handles transactions automatically via CS pin
    // This is a no-op, but kept for compatibility
}

void Adafruit_SPIDevice::beginTransactionWithAssertingCS() {
    if (_cs >= 0) {
        digitalWrite(_cs, LOW);
    }
}

void Adafruit_SPIDevice::endTransactionWithDeassertingCS() {
    if (_cs >= 0) {
        digitalWrite(_cs, HIGH);
    }
}

void Adafruit_SPIDevice::setChipSelect(int value) {
    if (_cs >= 0) {
        digitalWrite(_cs, value ? HIGH : LOW);
    }
}

size_t Adafruit_SPIDevice::write(uint8_t data) {
    return transfer(data);
}

size_t Adafruit_SPIDevice::write(const uint8_t* buffer, size_t len) {
    if (!_begun || spi_device_ == nullptr) {
        return 0;
    }
    
    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = buffer;
    t.rx_buffer = nullptr;
    t.flags = 0;
    
    esp_err_t ret = spi_device_transmit(spi_device_, &t);
    if (ret != ESP_OK) {
        return 0;
    }
    
    return len;
}

