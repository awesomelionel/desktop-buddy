#pragma once

#include <stdint.h>

#include "../Card.h"

class UpdateManager;

// Full-screen takeover during OTA install. Force-shown by
// CardController whenever UpdateManager is in Downloading or
// InstallReady; not part of the carousel.
class UpdatingCard : public Card {
public:
    explicit UpdatingCard(UpdateManager& um) : um_(um) {}

    void tick(uint32_t now_ms) override;
    void invalidate() override;
    bool isDirty() const override;
    void render(Display& display) override;

    // True while UpdateManager is mid-install. CardController polls
    // this each tick and pre-empts the carousel when true.
    bool isActive() const;

private:
    UpdateManager& um_;
    bool      full_clear_     = true;
    uint8_t   last_pct_drawn_ = 255;     // sentinel — guarantees first paint
    uint32_t  last_render_ms_ = 0;
    uint32_t  now_ms_         = 0;
};
