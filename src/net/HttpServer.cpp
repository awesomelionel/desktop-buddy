#include "HttpServer.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "../core/AppState.h"
#include "../core/ConfigStore.h"
#include "WifiManager.h"

namespace {
constexpr uint16_t HTTP_PORT      = 80;
constexpr uint8_t  DNS_PORT       = 53;
constexpr const char* MDNS_HOST   = "claude-buddy";
}  // namespace

HttpServer::HttpServer(const WifiManager& wifi, const AppState& app, ConfigStore& config)
    : wifi_(wifi), app_(app), config_(config),
      server_(nullptr), dns_(nullptr), role_(Role::NONE), boot_ms_(0) {}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::begin() {
    boot_ms_ = millis();
}

void HttpServer::tick(uint32_t /*now_ms*/) {
    // Cheap state-driven role machine: react to WifiManager transitions.
    Role want = Role::NONE;
    switch (wifi_.state()) {
        case WifiState::STA_CONNECTED:   want = Role::STA; break;
        case WifiState::AP_PROVISIONING: want = Role::AP;  break;
        default: want = Role::NONE; break;
    }

    if (want != role_) {
        stop();
        if (want == Role::STA) startSta();
        else if (want == Role::AP) startAp();
    }

    if (server_) server_->handleClient();
    if (dns_)    dns_->processNextRequest();
}

void HttpServer::startSta() {
    server_ = new WebServer(HTTP_PORT);
    registerStaHandlers();
    server_->begin();

    if (MDNS.begin(MDNS_HOST)) {
        MDNS.addService("http", "tcp", HTTP_PORT);
        Serial.printf("[http] STA listening on http://%s.local (%s)\n",
                      MDNS_HOST, WiFi.localIP().toString().c_str());
    } else {
        Serial.printf("[http] STA listening on %s (mDNS unavailable)\n",
                      WiFi.localIP().toString().c_str());
    }

    role_ = Role::STA;
}

void HttpServer::startAp() {
    // AP startup proper happens in WifiManager step 5. This branch is
    // reachable now via the AP_PROVISIONING placeholder state, but does
    // nothing until then.
    role_ = Role::AP;
}

void HttpServer::stop() {
    if (server_) {
        server_->stop();
        delete server_;
        server_ = nullptr;
    }
    if (dns_) {
        dns_->stop();
        delete dns_;
        dns_ = nullptr;
    }
    if (role_ == Role::STA) MDNS.end();
    role_ = Role::NONE;
}

void HttpServer::registerStaHandlers() {
    server_->on("/status", HTTP_GET, [this]() {
        JsonDocument doc;
        doc["state"]    = wifi_.stateName();
        doc["ssid"]     = wifi_.ssid();
        doc["ip"]       = wifi_.ip().toString();
        doc["uptime_ms"] = millis() - boot_ms_;
        doc["device"]   = app_.deviceName();
        doc["live"]     = app_.isLive(millis());
        doc["claude_state"] = state_name(app_.buddyState());
        doc["status"]["total"]   = app_.status().total;
        doc["status"]["running"] = app_.status().running;
        doc["status"]["waiting"] = app_.status().waiting;
        doc["status"]["msg"]     = app_.status().msg;

        String out;
        serializeJson(doc, out);
        server_->send(200, "application/json", out);
    });

    server_->onNotFound([this]() {
        server_->send(404, "text/plain", "not found");
    });
}

void HttpServer::registerApHandlers() {
    // Captive portal handlers land in step 5.
}
