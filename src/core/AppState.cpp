#include "AppState.h"

#include <Arduino.h>
#include <esp_mac.h>
#include <stdio.h>
#include <string.h>

#include "Settings.h"

AppState::AppState()
    : mac_device_name_{},
      status_{}, buddy_state_(STATE_DISCONNECTED), last_snapshot_ms_(0) {
    strncpy(mac_device_name_, "Claude", sizeof(mac_device_name_) - 1);
    mac_device_name_[sizeof(mac_device_name_) - 1] = 0;
}

void AppState::initMacDeviceName() {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(mac_device_name_, sizeof(mac_device_name_),
             "Claude-%02X%02X", mac[4], mac[5]);
}

const char* AppState::deviceName() const {
    if (settings_) return settings_->data().device_name;
    return mac_device_name_;
}

bool AppState::isLive(uint32_t now_ms) const {
    if (last_snapshot_ms_ == 0) return false;
    uint32_t timeout_ms = settings_
        ? static_cast<uint32_t>(settings_->data().live_timeout_s) * 1000UL
        : 30000UL;
    return (now_ms - last_snapshot_ms_) <= timeout_ms;
}
