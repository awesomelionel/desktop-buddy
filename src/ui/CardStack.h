#pragma once

#include <stdint.h>
#include <vector>

#include "Card.h"

class Display;

// Vector of Cards with a current index for the bottom-of-stack carousel
// (next/prev nav), plus an "overlay" slot that can be pushed on top to
// pre-empt the carousel (e.g. permission prompt). When an overlay is set,
// the active card for rendering and input is the overlay.
class CardStack {
public:
    void addCard(Card* card);

    // Removes all carousel cards (overlay is also dropped). After clear(),
    // the next active() returns nullptr until cards are re-added.
    void clear();

    // Set the carousel index directly (clamped). Invalidates the now-active
    // card so it repaints. No-op if an overlay is set.
    void setIndex(size_t i);

    // Push an overlay card on top of the carousel. Pop with clearOverlay().
    // Both transitions invalidate the now-visible card so it repaints.
    void pushOverlay(Card* card);
    void clearOverlay();
    bool hasOverlay() const { return overlay_ != nullptr; }

    // Carousel navigation. No-ops when an overlay is active.
    void next();
    void prev();

    // Active card = overlay if present, else carousel[index_].
    Card* active() const;

    // Per-tick: tick + render the active card if dirty.
    void tick(uint32_t now_ms, Display& display);

    // Routes a button to the active card. Returns true if consumed.
    bool handleButton(ButtonEvent ev, uint32_t now_ms);

    size_t size()  const { return cards_.size(); }
    size_t index() const { return index_; }

private:
    std::vector<Card*> cards_;
    size_t             index_   = 0;
    Card*              overlay_ = nullptr;
};
