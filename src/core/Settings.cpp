#include "Settings.h"

#include <Preferences.h>
#include <string.h>

#include "EventBus.h"

namespace {
constexpr const char* NS       = "settings";
constexpr const char* KEY_V2   = "v2";    // current; includes bus_stops + 8-card model
constexpr const char* KEY_V1   = "v1";    // legacy; pre-bus-stops, 4-card model

// Mirror of the legacy v1 layout (pre-bus-stops, CARD_COUNT was 4).
// Kept here as the only producer of this shape — never added to public headers.
struct LegacyV1 {
    char     device_name[16];
    uint16_t live_timeout_s;
    uint16_t sleep_timeout_s;
    uint16_t dim_timeout_s;
    uint8_t  dim_level_pct;
    uint8_t  full_level_pct;
    uint32_t daily_token_cap;
    uint8_t  cards_enabled_mask;
    uint8_t  cards_order[4];
    uint8_t  cards_order_count;
    uint8_t  boot_card_id;
};
}  // namespace

Settings::Settings() {
    settings::setDefaults(data_, "Claude");
}

void Settings::begin(const char* default_name) {
    settings::setDefaults(data_, default_name);

    Preferences p;
    if (!p.begin(NS, /*readOnly=*/true)) return;

    // Try v2 first.
    settings::Settings stored = data_;
    size_t n = p.getBytesLength(KEY_V2);
    if (n == sizeof(settings::Settings)) {
        p.getBytes(KEY_V2, &stored, sizeof(stored));
        char err[64] = {};
        if (settings::validate(stored, err, sizeof(err))) {
            data_ = stored;
            p.end();
            return;
        }
    }

    // Fall back to v1: copy the overlapping fields, leave bus_stops zero,
    // and persist the upgraded blob so future boots take the v2 path.
    n = p.getBytesLength(KEY_V1);
    if (n == sizeof(LegacyV1)) {
        LegacyV1 v1{};
        p.getBytes(KEY_V1, &v1, sizeof(v1));
        // Map legacy fields into the new struct (which already has defaults
        // applied above, so bus_stops are zero-initialised).
        memcpy(data_.device_name, v1.device_name, sizeof(v1.device_name));
        data_.device_name[sizeof(v1.device_name) - 1] = '\0';
        data_.live_timeout_s    = v1.live_timeout_s;
        data_.sleep_timeout_s   = v1.sleep_timeout_s;
        data_.dim_timeout_s     = v1.dim_timeout_s;
        data_.dim_level_pct     = v1.dim_level_pct;
        data_.full_level_pct    = v1.full_level_pct;
        data_.daily_token_cap   = v1.daily_token_cap;
        data_.cards_enabled_mask = v1.cards_enabled_mask;
        for (uint8_t i = 0; i < 4; ++i) data_.cards_order[i] = v1.cards_order[i];
        for (uint8_t i = 4; i < settings::CARD_COUNT; ++i) data_.cards_order[i] = 0;
        data_.cards_order_count = v1.cards_order_count;
        data_.boot_card_id      = v1.boot_card_id;

        char err[64] = {};
        if (settings::validate(data_, err, sizeof(err))) {
            p.end();
            persist();   // writes the v2 blob
            return;
        }
        // Validation failed — fall through to defaults already in data_.
        settings::setDefaults(data_, default_name);
    }

    p.end();
}

bool Settings::applyDevice(const char* name,
                           uint16_t live_timeout_s,
                           uint16_t sleep_timeout_s,
                           char* err, size_t err_len) {
    settings::Settings next = data_;
    if (!settings::applyDeviceFields(next, name, live_timeout_s, sleep_timeout_s,
                                     err, err_len)) {
        return false;
    }
    data_ = next;
    persist();
    if (bus_) bus_->publish(EventKind::SettingsChanged);
    return true;
}

bool Settings::applyCards(uint8_t enabled_mask,
                          const uint8_t* order, uint8_t count,
                          uint8_t boot_card_id,
                          char* err, size_t err_len) {
    settings::Settings next = data_;
    if (!settings::applyCardsFields(next, enabled_mask, order, count, boot_card_id,
                                    err, err_len)) {
        return false;
    }
    data_ = next;
    persist();
    if (bus_) bus_->publish(EventKind::SettingsChanged);
    return true;
}

bool Settings::applyBacklight(uint16_t dim_timeout_s,
                              uint8_t  dim_level_pct,
                              uint8_t  full_level_pct,
                              char* err, size_t err_len) {
    settings::Settings next = data_;
    if (!settings::applyBacklightFields(next, dim_timeout_s, dim_level_pct,
                                        full_level_pct, err, err_len)) {
        return false;
    }
    data_ = next;
    persist();
    if (bus_) bus_->publish(EventKind::SettingsChanged);
    return true;
}

bool Settings::applyDailyCap(uint32_t daily_token_cap,
                             char* err, size_t err_len) {
    settings::Settings next = data_;
    if (!settings::applyDailyCapField(next, daily_token_cap, err, err_len)) {
        return false;
    }
    data_ = next;
    persist();
    if (bus_) bus_->publish(EventKind::SettingsChanged);
    return true;
}

bool Settings::applyBusStop(uint8_t slot,
                            const char* code,
                            const char* label,
                            char* err, size_t err_len) {
    settings::Settings next = data_;
    if (!settings::applyBusStopField(next, slot, code, label, err, err_len)) {
        return false;
    }
    data_ = next;
    persist();
    if (bus_) bus_->publish(EventKind::SettingsChanged);
    return true;
}

void Settings::clearToDefaults(const char* default_name) {
    Preferences p;
    if (p.begin(NS, /*readOnly=*/false)) {
        p.remove(KEY_V2);
        p.remove(KEY_V1);
        p.end();
    }
    settings::setDefaults(data_, default_name);
    if (bus_) bus_->publish(EventKind::SettingsChanged);
}

void Settings::persist() {
    Preferences p;
    if (!p.begin(NS, /*readOnly=*/false)) return;
    p.putBytes(KEY_V2, &data_, sizeof(data_));
    // Best-effort cleanup of the legacy blob; ignored if absent.
    p.remove(KEY_V1);
    p.end();
}
