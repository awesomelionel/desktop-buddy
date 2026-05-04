#pragma once

#include <stdint.h>

class Adafruit_MAX17048;

// Wraps the MAX17048 LiPoly fuel gauge that lives on the Reverse-TFT
// Feather's I2C bus (SDA=3, SCL=4, address 0x36). Exposes a cached
// percent + charging flag that the UI can read every frame without
// touching I2C. The gauge itself is polled at 1 Hz from tick().
//
// Charging detection uses the gauge's own chargeRate() (% per hour);
// it crosses zero with some noise, so we apply a small threshold and
// a couple of seconds of hysteresis to keep the UI from flickering
// between "charging" and "not charging" while a USB cable is plugged
// in but the cell is essentially full.
class Battery {
public:
    Battery();
    ~Battery();

    // Brings up Wire and probes the MAX17048. Safe to call once at
    // setup; if the chip is missing (no fuel gauge / pre-MAX board
    // revision), present_ stays false and the rest of the API returns
    // sentinel values.
    void begin();

    // Polls the gauge at 1 Hz and updates cached percent/charging.
    // Cheap to call every loop iteration — the I2C read only fires
    // when 1000 ms has elapsed since the last sample.
    void tick(uint32_t now_ms);

    // True iff begin() found a working fuel gauge.
    bool present() const { return present_; }

    // 0..100. 0xFF when not yet sampled or chip missing.
    uint8_t percent() const { return percent_; }

    // True while the cell is taking on charge. Driven by chargeRate()
    // with hysteresis (see CHARGE_ON / CHARGE_OFF in Battery.cpp).
    bool charging() const { return charging_; }

private:
    Adafruit_MAX17048* gauge_;
    bool               present_;
    uint8_t            percent_;
    bool               charging_;
    uint32_t           last_sample_ms_;
    // Last raw chargeRate sample, kept so the hysteresis logic in
    // tick() can decide when to flip charging_ on/off.
    float              last_rate_;
};
