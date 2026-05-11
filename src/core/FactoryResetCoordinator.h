#pragma once

#include <stdint.h>

#include "factory_reset_state.h"

class ConfigStore;
class Settings;
class InputRouter;

class FactoryResetCoordinator {
public:
    FactoryResetCoordinator(ConfigStore& cfg, Settings& settings,
                            const char* default_device_name);

    void setInputRouter(InputRouter* r) { input_ = r; }

    void tick(uint32_t now_ms);

    // Web endpoint calls this. Opens the 30-s arm window.
    void arm(uint32_t now_ms);

    factory_reset_state::Phase phase() const { return machine_.phase(); }
    uint32_t                   holdMs() const { return machine_.holdMs(); }

private:
    void performWipe();

    ConfigStore&  cfg_;
    Settings&     settings_;
    InputRouter*  input_ = nullptr;
    const char*   default_device_name_;

    factory_reset_state::Machine machine_;
    bool          wipe_done_ = false;
    uint32_t      wipe_started_ms_ = 0;
};
