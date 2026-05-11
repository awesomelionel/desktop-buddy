#include "factory_reset_state.h"

namespace factory_reset_state {

void Machine::arm(uint32_t now_ms) {
    phase_ = Phase::AwaitingHold;
    armed_at_ms_ = now_ms;
    hold_start_ms_ = 0;
    was_pressed_ = false;
    last_now_ms_ = now_ms;
}

void Machine::tick(const Inputs& in) {
    last_now_ms_ = in.now_ms;

    if (phase_ != Phase::AwaitingHold) {
        was_pressed_ = in.center_pressed;
        return;
    }

    // Window timeout (only while we're still in AwaitingHold and no
    // hold has confirmed).
    if (in.now_ms - armed_at_ms_ >= kArmWindowMs && !in.center_pressed) {
        phase_ = Phase::Idle;
        was_pressed_ = false;
        return;
    }

    if (in.center_pressed && !was_pressed_) {
        hold_start_ms_ = in.now_ms;          // press edge
    }
    if (!in.center_pressed) {
        hold_start_ms_ = 0;                  // release
    }

    if (in.center_pressed && hold_start_ms_ != 0) {
        uint32_t held = in.now_ms - hold_start_ms_;
        if (held >= kHoldRequiredMs) {
            phase_ = Phase::Resetting;
        }
    } else if (!in.center_pressed && was_pressed_) {
        // released before threshold → back to idle (caller dismisses card)
        phase_ = Phase::Idle;
    }

    was_pressed_ = in.center_pressed;
}

uint32_t Machine::holdMs() const {
    if (phase_ != Phase::AwaitingHold || hold_start_ms_ == 0) return 0;
    if (last_now_ms_ < hold_start_ms_) return 0;
    return last_now_ms_ - hold_start_ms_;
}

}  // namespace factory_reset_state
