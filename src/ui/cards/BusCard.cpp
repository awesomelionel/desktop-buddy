#include "BusCard.h"

#include <Arduino.h>
#include <string.h>

#include "../../display/Display.h"

BusCard::BusCard(uint8_t slot_index,
                 const Settings& settings,
                 const WifiManager& wifi)
    : slot_(slot_index),
      settings_(settings),
      wifi_(wifi),
      data_{},
      last_fetch_attempt_ms_(0),
      shown_at_ms_(0),
      last_tick_minute_(0),
      first_visible_(0),
      ever_drawn_(false),
      visible_(false),
      dirty_(true),
      last_drawn_first_visible_(0),
      last_drawn_label_{0},
      last_drawn_code_{0},
      last_drawn_wifi_up_(false),
      last_drawn_state_(DisplayState::Loading) {
    for (uint8_t i = 0; i < kViewportRows; ++i) {
        last_drawn_[i] = {};
        last_drawn_minute_[i] = INT32_MIN;
    }
}

void BusCard::invalidate() {
    ever_drawn_ = false;
    dirty_      = true;
    last_drawn_first_visible_ = 0xFF;
    last_drawn_state_         = DisplayState::Loading;
    for (uint8_t i = 0; i < kViewportRows; ++i) {
        last_drawn_[i] = {};
        last_drawn_minute_[i] = INT32_MIN;
    }
    last_drawn_label_[0] = '\xFF';
    last_drawn_code_[0]  = '\xFF';
    last_drawn_wifi_up_  = !wifi_.isConnected();   // force flip
}

bool BusCard::isDirty() const {
    return dirty_ || !ever_drawn_;
}

void BusCard::onShow() {
    visible_ = true;
    shown_at_ms_ = millis();
    // Trigger an immediate fetch on the next tick.
    last_fetch_attempt_ms_ = 0;
    dirty_ = true;
}

void BusCard::onHide() {
    visible_ = false;
}

void BusCard::tick(uint32_t now_ms) {
    if (!visible_) return;

    if (!wifi_.isConnected()) {
        if (last_drawn_wifi_up_) dirty_ = true;
        return;
    }

    if (shouldFetch(now_ms)) {
        doFetch(now_ms);
        dirty_ = true;
        return;
    }

    if (data_.valid) {
        uint32_t minute = (now_ms - data_.fetched_at_ms) / 60000;
        if (minute != last_tick_minute_) {
            last_tick_minute_ = minute;
            dirty_ = true;
        }
    }
}

bool BusCard::shouldFetch(uint32_t now_ms) const {
    if (last_fetch_attempt_ms_ == 0) return true;
    return (now_ms - last_fetch_attempt_ms_) >= kFetchPeriodMs;
}

void BusCard::doFetch(uint32_t now_ms) {
    last_fetch_attempt_ms_ = now_ms;
    const char* code = settings_.data().bus_stops[slot_].code;
    if (code[0] == '\0') {
        // Slot got cleared while card was in stack; rebuildStack will
        // remove us shortly. Nothing to do.
        return;
    }
    fetcher_.fetch(code, now_ms, data_);
    last_tick_minute_ = 0;
}

bool BusCard::handleButton(ButtonEvent ev, uint32_t now_ms) {
    (void)now_ms;
    if (ev == BTN_CENTER && data_.service_count > kViewportRows) {
        first_visible_ = (uint8_t)((first_visible_ + 1) % data_.service_count);
        dirty_ = true;
        return true;
    }
    return false;
}

int BusCard::displayedMinute(uint32_t now_ms,
                             const bus_arrivals::BusServiceArrival& svc) const {
    if (svc.eta_seconds_at_fetch == INT32_MIN) return INT32_MIN;
    int32_t elapsed = (int32_t)((now_ms - data_.fetched_at_ms) / 1000);
    int32_t remaining = svc.eta_seconds_at_fetch - elapsed;
    if (remaining <= 0) return 0;
    return (int)(remaining / 60);
}

BusCard::DisplayState BusCard::computeState(uint32_t now_ms) const {
    if (!wifi_.isConnected()) return DisplayState::NoWifi;
    if (!data_.valid) {
        if (data_.last_error[0] == '\0') return DisplayState::Loading;
        return DisplayState::FetchError;
    }
    // valid == true.
    if (data_.last_error[0] != '\0') {
        if ((now_ms - data_.last_fetch_success_ms) > kStaleAfterMs) {
            return DisplayState::Stale;
        }
    }
    if (data_.service_count == 0) return DisplayState::Empty;
    return DisplayState::Normal;
}

// Stub render-helper bodies. Filled in by Tasks 10 (header + center messages)
// and 11 (rows + scroll hint). Present here so any future caller links without
// extra source-tree churn.
void BusCard::renderHeader(Adafruit_ST7789& /*tft*/, DisplayState /*state*/) {}
void BusCard::renderRows(Adafruit_ST7789& /*tft*/, uint32_t /*now_ms*/) {}
void BusCard::renderCenterMessage(Adafruit_ST7789& /*tft*/,
                                  const char* /*line1*/, const char* /*line2*/) {}
void BusCard::renderScrollHint(Adafruit_ST7789& /*tft*/, uint32_t /*now_ms*/) {}
void BusCard::clearBody(Adafruit_ST7789& /*tft*/) {}

void BusCard::render(Display& /*display*/) {
    // Implemented in Task 10 (states + header) and Task 11 (rows).
    dirty_      = false;
    ever_drawn_ = true;
}
