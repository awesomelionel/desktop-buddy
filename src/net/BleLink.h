#pragma once

#include <stddef.h>
#include <stdint.h>

class AppState;
class EventBus;
class Settings;

// Wraps the C-style ble_bridge module: drains incoming bytes, accumulates
// them into a 4KB line buffer (per REFERENCE.md), parses each line into
// AppState, and exposes writeLine() for outgoing decisions.
class BleLink {
public:
    explicit BleLink(AppState& app);

    void begin(const char* device_name);

    // Optional: publishes EventKind::SnapshotReceived once per successful
    // line parse.
    void setEventBus(EventBus* bus) { bus_ = bus; }

    // Bind Settings so we can react to device-name changes via the
    // SettingsChanged event. Caller-owned; lifetime must outlive BleLink.
    void setSettings(const Settings* settings) { settings_ = settings; }

    // Subscribe handlers on the bound EventBus. Call after both
    // setEventBus() and setSettings() are wired.
    void registerEvents();

    // Drain available bytes; for each completed JSON line, parse into
    // appState and stamp markSnapshot(now_ms).
    void tick(uint32_t now_ms);

    // Send a single newline-terminated line. Returns true on success.
    bool writeLine(const char* line);

    bool isConnected() const;

private:
    AppState&       app_;
    EventBus*       bus_      = nullptr;
    const Settings* settings_ = nullptr;
    char            current_name_[16] = {0};
    char            prev_prompt_id_[40] = {0};
    uint32_t        prev_tokens_today_  = 0;

    // Snapshot lines can carry an entries[] transcript; REFERENCE.md caps
    // event payloads at 4KB. 4096 + 1 trailing null = max wire size.
    static constexpr size_t LINE_BUF_LEN = 4097;
    char    line_buf_[LINE_BUF_LEN];
    size_t  line_len_;
    bool    line_overflow_;
};
