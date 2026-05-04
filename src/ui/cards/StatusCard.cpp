#include "StatusCard.h"

#include <Arduino.h>

#include "../../display/Display.h"
#include "../Footer.h"
#include "format.h"

StatusCard::StatusCard(const AppState& state)
    : state_(state),
      ever_drawn_(false),
      last_drawn_state_(STATE_DISCONNECTED),
      last_drawn_total_(0xFF),       // sentinel: no real count == 0xFF
      last_drawn_running_(0xFF),
      last_drawn_waiting_(0xFF),
      last_drawn_valid_(false),
      last_drawn_msg_{0},
      last_drawn_live_(false),
      last_recheck_ms_(0),
      last_drawn_tokens_today_(0xFFFFFFFFu) {}

void StatusCard::invalidate() {
    ever_drawn_ = false;
    last_drawn_msg_[0] = 0;
}

void StatusCard::tick(uint32_t now_ms) {
    if (now_ms - last_recheck_ms_ > 1000) {
        last_recheck_ms_ = now_ms;
        // No-op: the live flag is read at render time. The 1s tick is just
        // here so isDirty() flips true once liveness changes due to timeout.
    }
}

bool StatusCard::isDirty() const {
    if (!ever_drawn_) return true;
    const ClaudeStatus& status = state_.status();
    if (state_.buddyState() != last_drawn_state_) return true;
    if (status.total   != last_drawn_total_)   return true;
    if (status.running != last_drawn_running_) return true;
    if (status.waiting != last_drawn_waiting_) return true;
    if (status.valid   != last_drawn_valid_)   return true;
    if (status.tokens_today != last_drawn_tokens_today_) return true;
    if (strncmp(last_drawn_msg_, status.msg, sizeof(last_drawn_msg_)) != 0) return true;
    if (state_.isLive(millis()) != last_drawn_live_) return true;
    return false;
}

void StatusCard::render(Display& display) {
    auto&               tft    = display.tft();
    const ClaudeStatus& status = state_.status();
    const BuddyState    bs     = state_.buddyState();
    const bool          live   = state_.isLive(millis());

    // Per-region rendering. fillScreen runs only on a state transition
    // (rare and meaningful — IDLE↔WORKING↔WAITING↔DISCONNECTED). Every
    // other dirty trip (msg / counters / tokens / live) erases just the
    // strip that owns the changing content, eliminating the whole-card
    // strobe the previous unconditional fillScreen produced. Per
    // CLAUDE.md: never fillScreen in a path that runs more than once
    // per state.
    const bool state_changed = !ever_drawn_ || (last_drawn_state_ != bs);

    if (state_changed) {
        tft.fillScreen(ST77XX_BLACK);

        // State name (size 3, white, centred at y=20). Only redrawn on a
        // state transition because the string is derived from `bs` and
        // can't change without state_changed being true.
        tft.setTextSize(3);
        tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        const char* name = state_name(bs);
        int16_t  x1, y1; uint16_t tw, th;
        tft.getTextBounds(name, 0, 0, &x1, &y1, &tw, &th);
        tft.setCursor((display.width() - (int)tw) / 2, 20);
        tft.print(name);
    }

    // Counters strip (size 1, cyan, y=58 .. 65). Repaint when any of
    // total/running/waiting changes. Erase as a 240×9 strip so any
    // shorter previous text (e.g. counts went from 10 → 9) doesn't
    // leave trailing chars.
    const bool counters_changed = state_changed ||
                                  (last_drawn_total_   != status.total)   ||
                                  (last_drawn_running_ != status.running) ||
                                  (last_drawn_waiting_ != status.waiting);
    if (counters_changed) {
        if (!state_changed) {
            tft.fillRect(0, 58, 240, 9, ST77XX_BLACK);
        }
        tft.setTextSize(1);
        tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
        tft.setCursor(8, 58);
        tft.printf("total %u  run %u  wait %u",
                   status.total, status.running, status.waiting);
    }

    // Daily token count line (size 2, white, centred at y=70 .. 85).
    // Hidden until status.valid flips true. Repaint when valid flips OR
    // when tokens_today changes. Erase as a 240×17 strip so the
    // previous (potentially differently-centred) line can't ghost.
    const bool token_changed = state_changed ||
                               (last_drawn_valid_         != status.valid) ||
                               (last_drawn_tokens_today_  != status.tokens_today);
    if (token_changed) {
        if (!state_changed) {
            tft.fillRect(0, 70, 240, 17, ST77XX_BLACK);
        }
        if (status.valid) {
            char tok_buf[kFormatTokenCountBufLen];
            format_token_count(status.tokens_today, tok_buf, sizeof(tok_buf));
            char tok_line[32];
            snprintf(tok_line, sizeof(tok_line), "%s tokens today", tok_buf);

            tft.setTextSize(2);
            tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
            int16_t tx1, ty1; uint16_t ttw, tth;
            tft.getTextBounds(tok_line, 0, 0, &tx1, &ty1, &ttw, &tth);
            tft.setCursor((display.width() - (int)ttw) / 2, 70);
            tft.print(tok_line);
        }
    }

    // Message block (size 1, white, two lines at y=92 and y=104).
    // Repaint when msg changes. Erase 240×24 to cover both lines plus
    // a 1-px margin so a shorter new message can't leave trailing
    // chars from the old one.
    const bool msg_changed = state_changed ||
                             (strncmp(last_drawn_msg_, status.msg,
                                      sizeof(last_drawn_msg_)) != 0);
    if (msg_changed) {
        if (!state_changed) {
            tft.fillRect(0, 92, 240, 24, ST77XX_BLACK);
        }
        tft.setTextSize(1);
        tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        if (status.msg[0]) {
            tft.setCursor(8, 92);
            tft.printf("%.34s", status.msg);
            if (strlen(status.msg) > 34) {
                tft.setCursor(8, 104);
                tft.printf("%.34s", status.msg + 34);
            }
        }
    }

    // Footer band (y=117 .. 134). Repaint when liveness flips. The
    // device name is treated as static here — drawFooter handles its
    // own internal layout.
    const bool footer_changed = state_changed || (last_drawn_live_ != live);
    if (footer_changed) {
        if (!state_changed) {
            tft.fillRect(0, ui::kFooterTopY, 240, ui::kFooterH, ST77XX_BLACK);
        }
        ui::drawFooter(tft, state_.deviceName(), live);
    }

    // Snapshot every tracked value.
    last_drawn_state_         = bs;
    last_drawn_total_         = status.total;
    last_drawn_running_       = status.running;
    last_drawn_waiting_       = status.waiting;
    last_drawn_valid_         = status.valid;
    strncpy(last_drawn_msg_, status.msg, sizeof(last_drawn_msg_) - 1);
    last_drawn_msg_[sizeof(last_drawn_msg_) - 1] = 0;
    last_drawn_live_          = live;
    last_drawn_tokens_today_  = status.tokens_today;
    ever_drawn_               = true;
}
