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
    server_ = new WebServer(HTTP_PORT);
    registerApHandlers();
    server_->begin();

    dns_ = new DNSServer();
    // Wildcard DNS: any A query on the AP returns 192.168.4.1 so the
    // captive portal grabs whatever URL the client probes.
    dns_->setErrorReplyCode(DNSReplyCode::NoError);
    dns_->start(DNS_PORT, "*", WiFi.softAPIP());

    Serial.printf("[http] AP listening on http://%s\n",
                  WiFi.softAPIP().toString().c_str());

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
    server_->on("/", HTTP_GET, [this]() {
        // Tiny zero-dep form. Keep under one MTU.
        String html =
            "<!doctype html><meta name=viewport content='width=device-width'>"
            "<title>claude-buddy setup</title>"
            "<style>body{font:16px sans-serif;margin:24px;max-width:360px}"
            "input,button{font:inherit;width:100%;padding:8px;margin:4px 0;"
            "box-sizing:border-box}label{display:block;margin-top:12px}</style>"
            "<h1>claude-buddy</h1><p>Pick a Wi-Fi network and enter the password.</p>"
            "<form method=POST action=/save>"
            "<label>SSID<input name=ssid maxlength=32 required></label>"
            "<label>Password<input name=pass type=password maxlength=64></label>"
            "<button type=submit>Save &amp; reboot</button></form>";
        server_->send(200, "text/html", html);
    });

    server_->on("/save", HTTP_POST, [this]() {
        if (!server_->hasArg("ssid")) {
            server_->send(400, "text/plain", "missing ssid");
            return;
        }
        String ssid = server_->arg("ssid");
        String pass = server_->arg("pass");
        if (!config_.setCreds(ssid.c_str(), pass.c_str())) {
            server_->send(400, "text/plain", "invalid ssid/password length");
            return;
        }
        server_->send(200, "text/html",
            "<!doctype html><title>saved</title>"
            "<h1>Saved</h1><p>Rebooting to join '"
            + ssid + "'...</p>");
        // Give the response a beat to flush before we drop the radio.
        delay(500);
        ESP.restart();
    });

    // Captive-portal probe URLs from common clients — redirect to /.
    auto redirect = [this]() {
        server_->sendHeader("Location", "http://192.168.4.1/", true);
        server_->send(302, "text/plain", "");
    };
    server_->on("/generate_204",          HTTP_GET, redirect);  // Android
    server_->on("/hotspot-detect.html",   HTTP_GET, redirect);  // iOS / macOS
    server_->on("/connecttest.txt",       HTTP_GET, redirect);  // Windows
    server_->on("/redirect",              HTTP_GET, redirect);
    server_->on("/ncsi.txt",              HTTP_GET, redirect);

    server_->onNotFound(redirect);
}
