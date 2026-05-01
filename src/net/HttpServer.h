#pragma once

#include <stdint.h>

class WebServer;
class DNSServer;
class WifiManager;
class ConfigStore;
class AppState;
class Settings;

// HTTP server with two roles, same WebServer instance:
//   STA mode → JSON API (start with GET /status)
//   AP  mode → captive portal (added in Wi-Fi step 5)
//
// HttpServer doesn't own the radio mode; it follows whatever WifiManager is
// in. Call onWifiStateChange() (or just call begin() once and tick() each
// loop) and it will start/stop endpoints as needed.
class HttpServer {
public:
    HttpServer(WifiManager& wifi, const AppState& app, ConfigStore& config,
               Settings& settings);
    ~HttpServer();

    void begin();
    void tick(uint32_t now_ms);

private:
    void startSta();
    void startAp();
    void stop();

    void registerStaHandlers();
    void registerApHandlers();

    enum class Role { NONE, STA, AP };
    WifiManager&    wifi_;
    const AppState& app_;
    ConfigStore&    config_;
    Settings&       settings_;

    WebServer*  server_;
    DNSServer*  dns_;
    Role        role_;
    uint32_t    boot_ms_;
};
