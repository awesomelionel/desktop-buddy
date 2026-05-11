#pragma once

#include <stdint.h>

namespace factory_reset_state {

enum class Phase : uint8_t {
    Idle,
    AwaitingHold,    // armed via web; user must hold center button
    Resetting,       // 3-s hold confirmed; caller should wipe NVS now
};

struct Inputs {
    uint32_t now_ms;
    bool     center_pressed;
};

constexpr uint32_t kArmWindowMs    = 30000;   // 30 s
constexpr uint32_t kHoldRequiredMs = 3000;    // 3 s

class Machine {
public:
    void   arm(uint32_t now_ms);
    void   tick(const Inputs& in);

    Phase    phase()  const { return phase_; }
    uint32_t holdMs() const;

private:
    Phase    phase_         = Phase::Idle;
    uint32_t armed_at_ms_   = 0;
    uint32_t hold_start_ms_ = 0;
    bool     was_pressed_   = false;
    uint32_t last_now_ms_   = 0;
};

}  // namespace factory_reset_state
