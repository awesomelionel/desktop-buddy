#include "buttons.h"

static const uint32_t T_DEBOUNCE = 20;

static void chan_init(ButtonChan* c) {
    c->stable      = false;
    c->last_raw    = false;
    c->last_change = 0;
    c->initialized = false;
}

// Advance one channel; return true if the debounced state transitioned
// from not-pressed to pressed during this step.
static bool chan_step(ButtonChan* c, uint32_t now, bool raw) {
    if (!c->initialized) {
        c->stable      = raw;
        c->last_raw    = raw;
        c->last_change = now;
        c->initialized = true;
        return false;  // never fire on the first sample
    }
    if (raw != c->last_raw) {
        c->last_raw    = raw;
        c->last_change = now;
        return false;
    }
    if (raw != c->stable && (now - c->last_change) >= T_DEBOUNCE) {
        bool was = c->stable;
        c->stable = raw;
        return raw && !was;
    }
    return false;
}

void buttons_init(Buttons* b) {
    chan_init(&b->up);
    chan_init(&b->down);
    chan_init(&b->center);
}

ButtonEvent buttons_step(Buttons* b, uint32_t now_ms,
                         bool up_raw, bool down_raw, bool center_raw) {
    bool up_fire     = chan_step(&b->up,     now_ms, up_raw);
    bool down_fire   = chan_step(&b->down,   now_ms, down_raw);
    bool center_fire = chan_step(&b->center, now_ms, center_raw);

    // Priority: CENTER > DOWN > UP. Lower-priority fires are absorbed
    // (their state already advanced inside chan_step, just discard).
    if (center_fire) return BTN_CENTER;
    if (down_fire)   return BTN_DOWN;
    if (up_fire)     return BTN_UP;
    return BTN_NONE;
}
