/**
 * @file Adafruit_I2CDevice.cpp
 * @brief ESP-IDF v5.5 native I2C device implementation for ESP32-C6
 * 
 * Complete I2C implementation compatible with ESP-IDF v5.5 and ESP32-C6.
 * Supports multiple I2C buses, configurable pins, and proper error handling.
 */

#include "Adafruit_I2CDevice.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#include <cstring>
#include <cstdlib>

static const char* TAG_I2C = "I2CDevice";

// Global I2C bus handles (one per port) - shared across all devices on same bus
// ESP32-C6 and most ESP32 variants support 2 I2C ports (I2C_NUM_0 and I2C_NUM_1)
#define MAX_I2C_PORTS 2
static i2c_master_bus_handle_t s_global_i2c_bus[MAX_I2C_PORTS] = {nullptr};
static bool s_bus_initialized[MAX_I2C_PORTS] = {false};
static gpio_num_t s_bus_sda_pin[MAX_I2C_PORTS] = {GPIO_NUM_NC};
static gpio_num_t s_bus_scl_pin[MAX_I2C_PORTS] = {GPIO_NUM_NC};

// Default I2C pins (chip-specific defaults)
gpio_num_t Adafruit_I2CDevice::s_default_sda = GPIO_NUM_NC;
gpio_num_t Adafruit_I2CDevice::s_default_scl = GPIO_NUM_NC;
uint32_t Adafruit_I2CDevice::s_default_freq = 100000;
i2c_port_num_t Adafruit_I2CDevice::s_default_port = I2C_NUM_0;

Adafruit_I2CDevice::Adafruit_I2CDevice(uint8_t addr, void* theWire)
    : addr_(addr), bus_handle_(nullptr), device_handle_(nullptr), 
      initialized_(false), sda_pin_(s_default_sda), scl_pin_(s_default_scl), 
      i2c_freq_(s_default_freq), i2c_port_(s_default_port) {
    // If theWire is provided, use it (cast from void*)
    if (theWire) {
        bus_handle_ = static_cast<i2c_master_bus_handle_t>(theWire);
    }
    
    // Initialize default pins if not set
    if (s_default_sda == GPIO_NUM_NC) {
        s_default_sda = getDefaultSda();
    }
    if (s_default_scl == GPIO_NUM_NC) {
        s_default_scl = getDefaultScl();
    }
    
    // Use defaults if pins not set
    if (sda_pin_ == GPIO_NUM_NC) {
        sda_pin_ = s_default_sda;
    }
    if (scl_pin_ == GPIO_NUM_NC) {
        scl_pin_ = s_default_scl;
    }
}

Adafruit_I2CDevice::~Adafruit_I2CDevice() {
    end();
}

gpio_num_t Adafruit_I2CDevice::getDefaultSda() {
    // ESP32-C6 default I2C pins
    #ifdef CONFIG_IDF_TARGET_ESP32C6
        return GPIO_NUM_6;  // ESP32-C6 default SDA
    #elif defined(CONFIG_IDF_TARGET_ESP32S3)
        return GPIO_NUM_21;  // ESP32-S3 default SDA
    #elif defined(CONFIG_IDF_TARGET_ESP32C3)
        return GPIO_NUM_6;   // ESP32-C3 default SDA
    #else
        return GPIO_NUM_21;  // ESP32/ESP32-S2 default SDA
    #endif
}

gpio_num_t Adafruit_I2CDevice::getDefaultScl() {
    // ESP32-C6 default I2C pins
    #ifdef CONFIG_IDF_TARGET_ESP32C6
        return GPIO_NUM_7;  // ESP32-C6 default SCL
    #elif defined(CONFIG_IDF_TARGET_ESP32S3)
        return GPIO_NUM_22;  // ESP32-S3 default SCL
    #elif defined(CONFIG_IDF_TARGET_ESP32C3)
        return GPIO_NUM_7;   // ESP32-C3 default SCL
    #else
        return GPIO_NUM_22;  // ESP32/ESP32-S2 default SCL
    #endif
}

bool Adafruit_I2CDevice::initBus() {
    // If bus handle was provided in constructor, use it
    if (bus_handle_ != nullptr) {
        return true;
    }
    
    // Validate port number
    if (i2c_port_ >= MAX_I2C_PORTS) {
        ESP_LOGE(TAG_I2C, "Invalid I2C port: %d (max: %d)", i2c_port_, MAX_I2C_PORTS - 1);
        return false;
    }
    
    // Validate pins
    if (sda_pin_ == GPIO_NUM_NC || scl_pin_ == GPIO_NUM_NC) {
        ESP_LOGE(TAG_I2C, "Invalid I2C pins: SDA=%d, SCL=%d", sda_pin_, scl_pin_);
        return false;
    }
    
    // If bus is already initialized with same pins, reuse it
    if (s_bus_initialized[i2c_port_] && s_global_i2c_bus[i2c_port_] != nullptr) {
        // Check if pins match
        if (s_bus_sda_pin[i2c_port_] == sda_pin_ && s_bus_scl_pin[i2c_port_] == scl_pin_) {
            bus_handle_ = s_global_i2c_bus[i2c_port_];
            ESP_LOGD(TAG_I2C, "Reusing existing I2C bus on port %d", i2c_port_);
            return true;
        } else {
            ESP_LOGW(TAG_I2C, "I2C port %d already initialized with different pins (SDA=%d, SCL=%d), "
                     "requested (SDA=%d, SCL=%d). Using existing bus.", 
                     i2c_port_, s_bus_sda_pin[i2c_port_], s_bus_scl_pin[i2c_port_], 
                     sda_pin_, scl_pin_);
            // Still use existing bus - ESP-IDF allows multiple devices on same bus
            bus_handle_ = s_global_i2c_bus[i2c_port_];
            return true;
        }
    }
    
    // Initialize I2C bus using configured pins
    // CRITICAL: Must use memset for ESP-IDF v5.5 structs with unions/flags
    i2c_master_bus_config_t i2c_bus_config;
    memset(&i2c_bus_config, 0, sizeof(i2c_bus_config));
    i2c_bus_config.i2c_port = i2c_port_;
    i2c_bus_config.sda_io_num = sda_pin_;
    i2c_bus_config.scl_io_num = scl_pin_;
    i2c_bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_bus_config.glitch_ignore_cnt = 7;
    i2c_bus_config.flags.enable_internal_pullup = true;
    
    esp_err_t ret = i2c_new_master_bus(&i2c_bus_config, &bus_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_I2C, "Failed to create I2C bus on port %d: %s (SDA=GPIO%d, SCL=GPIO%d)", 
                 i2c_port_, esp_err_to_name(ret), sda_pin_, scl_pin_);
        return false;
    }
    
    // Store bus handle and pins
    s_global_i2c_bus[i2c_port_] = bus_handle_;
    s_bus_initialized[i2c_port_] = true;
    s_bus_sda_pin[i2c_port_] = sda_pin_;
    s_bus_scl_pin[i2c_port_] = scl_pin_;
    
    ESP_LOGI(TAG_I2C, "I2C bus initialized: Port=%d, SDA=GPIO%d, SCL=GPIO%d, Freq=%lu Hz", 
             i2c_port_, sda_pin_, scl_pin_, (unsigned long)i2c_freq_);
    return true;
}

bool Adafruit_I2CDevice::begin(bool addr_detect) {
    if (initialized_) {
        return true;
    }
    
    // Initialize I2C bus if needed
    if (!initBus()) {
        return false;
    }
    
    // Configure I2C device
    // CRITICAL: Must use memset for ESP-IDF v5.5 structs with unions/flags
    i2c_device_config_t dev_cfg;
    memset(&dev_cfg, 0, sizeof(dev_cfg));
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = addr_;
    dev_cfg.scl_speed_hz = i2c_freq_;
    
    esp_err_t ret = i2c_master_bus_add_device(bus_handle_, &dev_cfg, &device_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_I2C, "Failed to add I2C device (addr=0x%02X) to bus: %s", 
                 addr_, esp_err_to_name(ret));
        return false;
    }
    
    initialized_ = true;
    
    // Optional: Detect device
    // NOTE: Detection disabled by default for OLED displays (0x3C, 0x3D) as they can 
    // enter invalid state when probed with dummy reads before proper initialization
    bool skip_detection = (addr_ == 0x3C || addr_ == 0x3D);  // Common OLED addresses
    
    if (addr_detect && !skip_detection) {
        if (!detected()) {
            ESP_LOGW(TAG_I2C, "I2C device at 0x%02X not detected (may be normal for some devices)", addr_);
            // Don't fail - device might be slow to respond or not support detection
        } else {
            ESP_LOGI(TAG_I2C, "I2C device at 0x%02X detected", addr_);
        }
    }
    
    ESP_LOGI(TAG_I2C, "I2C device initialized: addr=0x%02X, port=%d, freq=%lu Hz", 
             addr_, i2c_port_, (unsigned long)i2c_freq_);
    return true;
}

void Adafruit_I2CDevice::end() {
    if (device_handle_) {
        esp_err_t ret = i2c_master_bus_rm_device(device_handle_);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG_I2C, "Failed to remove I2C device: %s", esp_err_to_name(ret));
        }
        device_handle_ = nullptr;
    }
}

bool Adafruit_I2CDevice::detected() {
    if (!initialized_ || !bus_handle_) {
        return false;
    }
    
    // Use i2c_master_probe for detection (ESP-IDF v5.5+)
    // This sends a START + ADDR + WRITE bit and checks for ACK
    esp_err_t ret = i2c_master_probe(bus_handle_, addr_, pdMS_TO_TICKS(1000));
    return (ret == ESP_OK);
}

bool Adafruit_I2CDevice::read(uint8_t *buffer, size_t len, bool stop) {
    if (!initialized_ || !device_handle_ || !buffer || len == 0) {
        ESP_LOGE(TAG_I2C, "I2C read: Invalid parameters (initialized=%d, handle=%p, buffer=%p, len=%zu)", 
                 initialized_, device_handle_, buffer, len);
        return false;
    }
    
    esp_err_t ret = i2c_master_receive(device_handle_, buffer, len, pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_I2C, "I2C read failed (addr=0x%02X, len=%zu): %s", 
                 addr_, len, esp_err_to_name(ret));
        return false;
    }
    
    return true;
}

bool Adafruit_I2CDevice::write(const uint8_t *buffer, size_t len, bool stop,
                               const uint8_t *prefix_buffer, size_t prefix_len) {
    if (!initialized_ || !device_handle_ || !buffer || len == 0) {
        ESP_LOGE(TAG_I2C, "I2C write: Invalid parameters (initialized=%d, handle=%p, buffer=%p, len=%zu)", 
                 initialized_, device_handle_, buffer, len);
        return false;
    }
    
    // Combine prefix and data if prefix is provided
    if (prefix_buffer && prefix_len > 0) {
        // Use stack buffer for small transfers, heap for large ones
        const size_t max_stack_size = 64;
        size_t total_len = prefix_len + len;
        
        if (total_len <= max_stack_size) {
            uint8_t combined[max_stack_size];
            memcpy(combined, prefix_buffer, prefix_len);
            memcpy(combined + prefix_len, buffer, len);
            
            esp_err_t ret = i2c_master_transmit(device_handle_, combined, total_len, pdMS_TO_TICKS(1000));
            if (ret != ESP_OK) {
                ESP_LOGE(TAG_I2C, "I2C write failed (addr=0x%02X, len=%zu): %s", 
                         addr_, total_len, esp_err_to_name(ret));
                return false;
            }
        } else {
            // Large transfer - use dynamic allocation
            uint8_t* combined = (uint8_t*)malloc(total_len);
            if (!combined) {
                ESP_LOGE(TAG_I2C, "I2C write: Failed to allocate buffer for %zu bytes", total_len);
                return false;
            }
            memcpy(combined, prefix_buffer, prefix_len);
            memcpy(combined + prefix_len, buffer, len);
            
            esp_err_t ret = i2c_master_transmit(device_handle_, combined, total_len, pdMS_TO_TICKS(1000));
            free(combined);
            
            if (ret != ESP_OK) {
                ESP_LOGE(TAG_I2C, "I2C write failed (addr=0x%02X, len=%zu): %s", 
                         addr_, total_len, esp_err_to_name(ret));
                return false;
            }
        }
    } else {
        esp_err_t ret = i2c_master_transmit(device_handle_, buffer, len, pdMS_TO_TICKS(1000));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG_I2C, "I2C write failed (addr=0x%02X, len=%zu): %s", 
                     addr_, len, esp_err_to_name(ret));
            return false;
        }
    }
    
    return true;
}

bool Adafruit_I2CDevice::write_then_read(const uint8_t *write_buffer, size_t write_len,
                                         uint8_t *read_buffer, size_t read_len, bool stop) {
    if (!initialized_ || !device_handle_) {
        ESP_LOGE(TAG_I2C, "I2C write_then_read: Device not initialized");
        return false;
    }
    
    if (write_buffer && write_len > 0) {
        esp_err_t ret = i2c_master_transmit(device_handle_, write_buffer, write_len, 
                                            pdMS_TO_TICKS(1000));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG_I2C, "I2C write_then_read: write failed (addr=0x%02X, len=%zu): %s", 
                     addr_, write_len, esp_err_to_name(ret));
            return false;
        }
    }
    
    if (read_buffer && read_len > 0) {
        esp_err_t ret = i2c_master_receive(device_handle_, read_buffer, read_len, 
                                          pdMS_TO_TICKS(1000));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG_I2C, "I2C write_then_read: read failed (addr=0x%02X, len=%zu): %s", 
                     addr_, read_len, esp_err_to_name(ret));
            return false;
        }
    }
    
    return true;
}

bool Adafruit_I2CDevice::setSpeed(uint32_t desiredclk) {
    // I2C speed is set per device in ESP-IDF, but we can't change it dynamically
    // Store desired frequency for future use (would need to recreate device to apply)
    i2c_freq_ = desiredclk;
    ESP_LOGW(TAG_I2C, "I2C speed change requested to %lu Hz, but device already initialized. "
             "Speed can only be changed before begin() is called.", (unsigned long)desiredclk);
    return true;
}

