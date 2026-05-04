#pragma once

#include <stdint.h>
#include <string.h>

#include "../Card.h"
#include "../../core/AppState.h"
#include "protocol.h"
#include "state.h"

class StatusCard : public Card {
public:
    explicit StatusCard(const AppState& state);

    void invalidate() override;
    bool isDirty() const override;
    void render(Display& display) override;

    // The status card needs a periodic recheck so live → offline transitions
    // repaint even if the snapshot stops arriving.
    void tick(uint32_t now_ms) override;

private:
    const AppState& state_;
    bool            ever_drawn_;
    BuddyState      last_drawn_state_;
    // Per-region snapshots so render() can repaint only the strips that
    // actually changed and avoid a full-screen fillScreen on every dirty
    // trip — that pattern strobed the whole card visibly each time
    // tokens_today, msg, or the live flag updated.
    uint8_t         last_drawn_total_;
    uint8_t         last_drawn_running_;
    uint8_t         last_drawn_waiting_;
    bool            last_drawn_valid_;
    char            last_drawn_msg_[sizeof(ClaudeStatus::msg)];
    bool            last_drawn_live_;
    uint32_t        last_recheck_ms_;
    uint32_t        last_drawn_tokens_today_;
};
