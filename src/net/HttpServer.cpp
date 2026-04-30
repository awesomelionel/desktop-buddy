#include "HttpServer.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include "../core/AppState.h"
#include "../core/ConfigStore.h"
#include "WifiManager.h"

namespace {
constexpr uint16_t HTTP_PORT = 80;
constexpr uint8_t  DNS_PORT  = 53;

// Minimal JSON string escaper for the /status endpoint. Handles the
// characters that JSON requires: \", \\, \n, \r, \t, and control chars
// below 0x20. Output is appended to `out`.
void appendJsonString(String& out, const char* s) {
    out += '"';
    if (s) {
        for (const char* p = s; *p; ++p) {
            unsigned char c = static_cast<unsigned char>(*p);
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (c < 0x20) {
                        char esc[8];
                        snprintf(esc, sizeof(esc), "\\u%04x", c);
                        out += esc;
                    } else {
                        out += static_cast<char>(c);
                    }
            }
        }
    }
    out += '"';
}
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

    Serial.printf("[http] STA listening on http://%s\n",
                  WiFi.localIP().toString().c_str());

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
    role_ = Role::NONE;
}

void HttpServer::registerStaHandlers() {
    server_->on("/status", HTTP_GET, [this]() {
        // Hand-built JSON: ArduinoJson would pull in another ~10KB+ for
        // a single fixed-shape document. Only `msg` carries arbitrary
        // user payload, so it's the only field that goes through the
        // escaper.
        const ClaudeStatus& s    = app_.status();
        const uint32_t      now  = millis();
        String out;
        out.reserve(256);

        out += "{\"state\":";        appendJsonString(out, wifi_.stateName());
        out += ",\"ssid\":";          appendJsonString(out, wifi_.ssid());
        out += ",\"ip\":\"";          out += wifi_.ip().toString(); out += '"';
        out += ",\"uptime_ms\":";     out += String(now - boot_ms_);
        out += ",\"device\":";        appendJsonString(out, app_.deviceName());
        out += ",\"live\":";          out += app_.isLive(now) ? "true" : "false";
        out += ",\"claude_state\":";  appendJsonString(out, state_name(app_.buddyState()));
        out += ",\"status\":{\"total\":"; out += s.total;
        out += ",\"running\":";       out += s.running;
        out += ",\"waiting\":";       out += s.waiting;
        out += ",\"msg\":";           appendJsonString(out, s.msg);
        out += "}}";

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
