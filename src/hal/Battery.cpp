#include "Battery.h"

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MAX1704X.h>

namespace {
// Sample the fuel gauge at 1 Hz. The MAX17048 itself updates its SoC
// register about every ~250 ms internally, so faster polling on the
// I2C side just wastes power; slower than ~2 s lets a quick unplug
// take that long to reflect on screen.
constexpr uint32_t kSampleIntervalMs = 1000;

// chargeRate() returns %-per-hour. While discharging on a 330 mAh
// cell the rate sits in the low single digits negative; while a USB
// charger is connected the rate jumps to roughly +20..+60 %/h,
// dropping toward 0 as the cell tops off. We treat anything above
// CHARGE_ON as "charging" and anything below CHARGE_OFF as "not
// charging" — the gap is the hysteresis band.
constexpr float    kChargeOnRate  = 1.0f;   // start animating charge
constexpr float    kChargeOffRate = -0.5f;  // confirm discharge
}  // namespace

Battery::Battery()
    : gauge_(nullptr), present_(false), percent_(0xFF),
      charging_(false), last_sample_ms_(0), last_rate_(0.0f) {}

Battery::~Battery() {
    delete gauge_;
}

void Battery::begin() {
    Wire.begin();
    if (!gauge_) gauge_ = new Adafruit_MAX17048();
    present_ = gauge_->begin();
    if (!present_) {
        Serial.println("[battery] MAX17048 not found on I2C — battery widget will hide");
        return;
    }
    Serial.printf("[battery] MAX17048 detected (chip 0x%04X)\n", gauge_->getChipID());
    // Force a quickstart so the first reading is sane after a fresh
    // boot; without it the SoC reg can take a few seconds to settle.
    gauge_->quickStart();
    // Seed the cached values from a single read so the UI doesn't
    // flash an "unknown" sentinel for the first second of runtime.
    percent_      = (uint8_t)constrain((int)roundf(gauge_->cellPercent()), 0, 100);
    last_rate_    = gauge_->chargeRate();
    charging_     = (last_rate_ >= kChargeOnRate);
    last_sample_ms_ = millis();
}

void Battery::tick(uint32_t now_ms) {
    if (!present_) return;
    if ((now_ms - last_sample_ms_) < kSampleIntervalMs) return;
    last_sample_ms_ = now_ms;

    const float pct = gauge_->cellPercent();
    if (pct >= 0.0f && pct <= 200.0f) {
        // The gauge can briefly report >100 % when first plugged in;
        // clamp so the UI stays in [0,100].
        percent_ = (uint8_t)constrain((int)roundf(pct), 0, 100);
    }

    last_rate_ = gauge_->chargeRate();
    if (charging_) {
        if (last_rate_ <= kChargeOffRate) charging_ = false;
    } else {
        if (last_rate_ >= kChargeOnRate)  charging_ = true;
    }
}
