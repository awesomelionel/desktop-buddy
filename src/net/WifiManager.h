#pragma once

#include <stdint.h>
#include <IPAddress.h>

#include "../core/ConfigStore.h"

enum class WifiState : uint8_t {
    BOOT = 0,
    AP_PROVISIONING,
    STA_CONNECTING,
    STA_CONNECTED,
    STA_RECONNECT,
};

class WifiManager {
public:
    explicit WifiManager(ConfigStore& store);

    // Reads creds from ConfigStore. If present, kicks off STA_CONNECTING;
    // if absent, transitions to AP_PROVISIONING.
    void begin();

    // Drives the state machine: handles reconnect backoff.
    void tick(uint32_t now_ms);

    // Forces a re-read of creds and restarts the state machine. Call after
    // ConfigStore::setCreds() (e.g. from the captive portal save handler).
    void restart();

    WifiState   state()    const { return state_; }
    const char* stateName() const;
    const char* ssid()     const { return ssid_; }
    IPAddress   ip()       const;

    // True when state() == STA_CONNECTED *and* WiFi reports a usable IP.
    bool        isConnected() const;

private:
    void enterApProvisioning();
    void enterStaConnecting(uint32_t now_ms);
    void enterStaReconnect(uint32_t now_ms);
    void enterStaConnected();

    ConfigStore& store_;
    WifiState    state_;
    char         ssid_[ConfigStore::SSID_MAX_LEN + 1];
    char         password_[ConfigStore::PASS_MAX_LEN + 1];
    uint32_t     reconnect_at_ms_;
    uint32_t     reconnect_delay_ms_;  // exponential backoff
};
