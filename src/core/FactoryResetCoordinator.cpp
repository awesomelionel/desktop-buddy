#include "FactoryResetCoordinator.h"

#include <Arduino.h>

#include "ConfigStore.h"
#include "Settings.h"
#include "../input/InputRouter.h"

FactoryResetCoordinator::FactoryResetCoordinator(
    ConfigStore& cfg, Settings& settings, const char* default_device_name)
    : cfg_(cfg)
    , settings_(settings)
    , default_device_name_(default_device_name) {}

void FactoryResetCoordinator::arm(uint32_t now_ms) {
    wipe_done_ = false;
    machine_.arm(now_ms);
}

void FactoryResetCoordinator::tick(uint32_t now_ms) {
    factory_reset_state::Inputs in{
        now_ms,
        input_ ? input_->centerHeld() : false,
    };
    machine_.tick(in);

    if (machine_.phase() == factory_reset_state::Phase::Resetting
        && !wipe_done_) {
        performWipe();
        wipe_done_ = true;
        wipe_started_ms_ = now_ms;
    }

    // 1 s after the wipe, reboot.
    if (wipe_done_ && (now_ms - wipe_started_ms_) >= 1000) {
        delay(100);
        ESP.restart();
    }
}

void FactoryResetCoordinator::performWipe() {
    cfg_.clear();
    settings_.clearToDefaults(default_device_name_);
}
