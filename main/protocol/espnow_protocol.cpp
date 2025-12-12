/**
 * @file espnow_protocol.cpp
 * @brief ESP-NOW protocol implementation for remote controller
 */

#include "espnow_protocol.hpp"
#include "../config.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_netif.h"
#include "esp_event.h"
#include <cstring>

static const char* TAG_ = "espnow";

static QueueHandle_t s_proto_event_queue_ = nullptr;
static uint8_t s_next_msg_id_ = 1;

// recv queue from ISR to this module
static QueueHandle_t s_raw_recv_queue_ = nullptr;

struct RawMsg {
    uint8_t data[sizeof(espnow::EspNowPacket)];
    int     len;
};

// Forward declarations
static void espnow_recv_cb(const esp_now_recv_info_t* info, const uint8_t* data, int len);
static void espnow_send_cb(const wifi_tx_info_t* info, esp_now_send_status_t status);
static void recv_task(void*);
static void handle_packet(const espnow::EspNowPacket& pkt);

// -------- ESPNOW INIT --------

bool espnow::Init(QueueHandle_t event_queue) noexcept
{
    s_proto_event_queue_ = event_queue;
    s_raw_recv_queue_ = xQueueCreate(10, sizeof(RawMsg));

    // Initialize WiFi
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG_, "esp_netif_init failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG_, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return false;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_, "esp_wifi_set_storage failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG_, "esp_wifi_start failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_set_channel(WIFI_CHANNEL_, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_, "esp_wifi_set_channel failed: %s", esp_err_to_name(err));
        return false;
    }

    // Initialize ESP-NOW
    err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG_, "esp_now_init failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_now_register_recv_cb(espnow_recv_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_, "esp_now_register_recv_cb failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_now_register_send_cb(espnow_send_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_, "esp_now_register_send_cb failed: %s", esp_err_to_name(err));
        return false;
    }

    // Add peer (test unit MAC)
    esp_now_peer_info_t peer{};
    std::memcpy(peer.peer_addr, TEST_UNIT_MAC_, 6);
    peer.channel = WIFI_CHANNEL_;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(TAG_, "esp_now_add_peer failed: %s", esp_err_to_name(err));
        return false;
    }

    // Launch receive task
    xTaskCreate(recv_task, "espnow_recv", 4096, nullptr, 5, nullptr);

    ESP_LOGI(TAG_, "ESP-NOW initialized");
    return true;
}

// -------- PACKET BUILD / SEND HELPERS --------

static bool send_packet(uint8_t device_id, espnow::MsgType type, const void* payload, uint8_t payload_len) noexcept
{
    if (payload_len > espnow::MAX_PAYLOAD_SIZE_) {
        ESP_LOGE(TAG_, "Payload too big: %d", payload_len);
        return false;
    }

    espnow::EspNowPacket pkt{};
    pkt.hdr.sync = espnow::SYNC_BYTE_;
    pkt.hdr.version = espnow::PROTOCOL_VERSION_;
    pkt.hdr.device_id = device_id;
    pkt.hdr.type = static_cast<uint8_t>(type);
    pkt.hdr.id   = s_next_msg_id_++;
    pkt.hdr.len  = payload_len;

    if (payload_len && payload) {
        std::memcpy(pkt.payload, payload, payload_len);
    }

    size_t crc_len = sizeof(pkt.hdr) + payload_len;
    pkt.crc = espnow::crc16_ccitt(reinterpret_cast<uint8_t*>(&pkt), crc_len);

    size_t total_len = crc_len + sizeof(pkt.crc);
    esp_err_t err = esp_now_send(TEST_UNIT_MAC_, reinterpret_cast<uint8_t*>(&pkt), total_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_, "esp_now_send error: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

// -------- PUBLIC SEND FUNCTIONS --------

bool espnow::SendDeviceDiscovery() noexcept
{
    return send_packet(0, MsgType::DeviceDiscovery, nullptr, 0);
}

bool espnow::SendConfigRequest(uint8_t device_id) noexcept
{
    return send_packet(device_id, MsgType::ConfigRequest, nullptr, 0);
}

bool espnow::SendConfigSet(uint8_t device_id, const void* config_data, size_t config_len) noexcept
{
    if (config_len > MAX_PAYLOAD_SIZE_) {
        ESP_LOGE(TAG_, "Config data too large: %zu", config_len);
        return false;
    }
    return send_packet(device_id, MsgType::ConfigSet, config_data, static_cast<uint8_t>(config_len));
}

bool espnow::SendCommand(uint8_t device_id, uint8_t command_id, const void* payload, size_t payload_len) noexcept
{
    if (payload_len > MAX_PAYLOAD_SIZE_) {
        ESP_LOGE(TAG_, "Command payload too large: %zu", payload_len);
        return false;
    }
    return send_packet(device_id, MsgType::Command, payload, static_cast<uint8_t>(payload_len));
}

// -------- CALLBACKS & RECV TASK --------

static void espnow_send_cb(const wifi_tx_info_t* info, esp_now_send_status_t status)
{
    (void)info; // Unused in ESP-IDF v5.5
    ESP_LOGD(TAG_, "ESP-NOW send status=%s",
             status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

static void espnow_recv_cb(const esp_now_recv_info_t* info, const uint8_t* data, int len)
{
    (void)info; // Unused
    if (len <= 0 || len > (int)sizeof(espnow::EspNowPacket)) return;

    RawMsg msg{};
    msg.len = len;
    std::memcpy(msg.data, data, len);

    BaseType_t hpw = pdFALSE;
    xQueueSendFromISR(s_raw_recv_queue_, &msg, &hpw);
    if (hpw == pdTRUE) portYIELD_FROM_ISR();
}

static void handle_packet(const espnow::EspNowPacket& pkt)
{
    if (pkt.hdr.sync != espnow::SYNC_BYTE_) {
        ESP_LOGW(TAG_, "Bad SYNC 0x%02X", pkt.hdr.sync);
        return;
    }
    if (pkt.hdr.version != espnow::PROTOCOL_VERSION_) {
        ESP_LOGW(TAG_, "Unsupported protocol version: %d", pkt.hdr.version);
        return;
    }
    if (pkt.hdr.len > espnow::MAX_PAYLOAD_SIZE_) {
        ESP_LOGW(TAG_, "Bad LEN %d", pkt.hdr.len);
        return;
    }
    size_t crc_len = sizeof(pkt.hdr) + pkt.hdr.len;
    uint16_t calc = espnow::crc16_ccitt(reinterpret_cast<const uint8_t*>(&pkt), crc_len);
    if (calc != pkt.crc) {
        ESP_LOGW(TAG_, "CRC mismatch (calc=0x%04X recv=0x%04X)", calc, pkt.crc);
        return;
    }

    // Create event for higher layers
    espnow::ProtoEvent evt{};
    evt.type = static_cast<espnow::MsgType>(pkt.hdr.type);
    evt.device_id = pkt.hdr.device_id;
    evt.sequence_id = pkt.hdr.id;
    evt.payload_len = pkt.hdr.len;
    if (pkt.hdr.len > 0) {
        std::memcpy(evt.payload, pkt.payload, pkt.hdr.len);
    }

    if (s_proto_event_queue_) {
        xQueueSend(s_proto_event_queue_, &evt, 0);
    }
}

static void recv_task(void*)
{
    RawMsg msg{};
    while (true) {
        if (xQueueReceive(s_raw_recv_queue_, &msg, portMAX_DELAY) == pdTRUE) {
            const espnow::EspNowPacket* pkt = reinterpret_cast<const espnow::EspNowPacket*>(msg.data);
            handle_packet(*pkt);
        }
    }
}

