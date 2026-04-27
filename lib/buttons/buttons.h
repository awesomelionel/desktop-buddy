#pragma once
#include <stdint.h>
#include <stdbool.h>

// Polarity-agnostic debouncer for three buttons. The caller reads the
// hardware pins (with whatever pull and polarity it needs) and passes
// raw booleans where `true` means "pressed". Events fire on the
// transition from not-pressed to pressed of the *debounced* state.

enum ButtonEvent {
    BTN_NONE = 0,
    BTN_UP,
    BTN_DOWN,
    BTN_CENTER,
};

struct ButtonChan {
    bool     stable;        // current debounced state (true = pressed)
    bool     last_raw;      // last raw value seen
    uint32_t last_change;   // when last_raw last changed
    bool     initialized;   // false until the first step records baselines
};

struct Buttons {
    ButtonChan up;
    ButtonChan down;
    ButtonChan center;
};

void        buttons_init(struct Buttons* b);

// Advance the debouncer by one tick. `now_ms` should be a monotonic
// millisecond timestamp (millis()). Returns one event per call; if
// multiple channels would fire simultaneously, priority is
// CENTER > DOWN > UP and the others are absorbed (their pressed state
// is recorded but they emit nothing this call).
ButtonEvent buttons_step(struct Buttons* b, uint32_t now_ms,
                         bool up_raw, bool down_raw, bool center_raw);
