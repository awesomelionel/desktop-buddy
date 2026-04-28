# Disconnected Eyes (Asleep Look) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current `STATE_DISCONNECTED` 3-second-blank + 200 ms grey eye blip with a personable "asleep" look — closed eyelids and a continuous staggered trail of three uppercase Z glyphs drifting diagonally up-right, the entire time the device is disconnected.

**Architecture:** Time-driven animation in `eyes.cpp`. On entry to `STATE_DISCONNECTED`, capture `disc_anim_start_ms = millis()`. On every tick, cache `disc_age_ms = now - disc_anim_start_ms`. `eyes_render` derives all three Z's positions/sizes/colors from `disc_age_ms` modulo a 3000 ms loop, with phase offsets of 0/1000/2000 ms. No per-Z state. The dirty-check in `main.cpp` is updated to repaint when the cached age advances (matches the existing pattern for `WORKING`'s sine-driven dx).

**Tech Stack:** C++17, Arduino/ESP32, Adafruit_GFX, Adafruit_ST7789. Unity host tests (not exercised — change is pure render behavior on top of the TFT driver).

**Spec:** `docs/superpowers/specs/2026-04-28-disconnected-eyes-design.md`

---

## File Structure

**Modified:**
- `src/eyes.h` — swap pulse fields (`disc_phase`, `disc_next_ms`) for `disc_anim_start_ms` + `disc_age_ms` in `EyesAnim`.
- `src/eyes.cpp` — drop `tick_disconnected`, simplify the `STATE_DISCONNECTED` branches in `arm_state` / `eyes_tick` / `eyes_reset`, rewrite the `STATE_DISCONNECTED` path in `eyes_render` to draw the asleep look.
- `src/main.cpp` — replace `lastEyesDiscPhase` with `lastEyesDiscAge` in the eyes-card dirty-check.

**No new files. No test files.** The renderer touches `Adafruit_ST7789` directly, and the project's host tests don't currently mock that.

---

## Task 1: Swap `EyesAnim` disconnected fields

**Files:**
- Modify: `src/eyes.h:7-32`

- [ ] **Step 1: Replace the DISCONNECTED-pulse fields in `EyesAnim`**

In `src/eyes.h`, change:

```cpp
struct EyesAnim {
    BuddyState prev_state;

    // DISCONNECTED pulse
    uint8_t  disc_phase;    // 0 black, 1 dim eyes
    uint32_t disc_next_ms;  // next phase transition
```

to:

```cpp
struct EyesAnim {
    BuddyState prev_state;

    // DISCONNECTED asleep
    uint32_t disc_anim_start_ms;  // millis() at entry to STATE_DISCONNECTED
    uint32_t disc_age_ms;         // cached for render: (now - disc_anim_start_ms)
```

Leave every other field in `EyesAnim` exactly as it is. Leave the function declarations at the bottom of the file unchanged.

- [ ] **Step 2: Verify the file compiles standalone (sanity, no commit yet)**

Run:
```bash
pio run -e adafruit_feather_esp32s3_reversetft -t checkprogsize 2>&1 | tail -40
```

Expected: build will FAIL because `eyes.cpp` and `main.cpp` still reference the removed fields. That's fine — proceed to Task 2.

---

## Task 2: Rewrite `STATE_DISCONNECTED` paths in `eyes.cpp`

**Files:**
- Modify: `src/eyes.cpp`

- [ ] **Step 1: Add new constants in the anonymous namespace**

In `src/eyes.cpp`, find the constant block at the top (around lines 8–22). Add these new constants directly below the existing ones (immediately above `const uint8_t kBlinkH[]`):

```cpp
const int      kLidH       = 10;
const int      kZSpawnX    = 210;
const int      kZSpawnY    = 50;
const int      kZDriftX    = 20;
const int      kZDriftY    = -45;
const uint32_t kZLoopMs    = 3000;
```

Leave every other constant unchanged.

- [ ] **Step 2: Update `arm_state` for `STATE_DISCONNECTED`**

In `src/eyes.cpp`, replace:

```cpp
    case STATE_DISCONNECTED:
        e.disc_phase    = 0;
        e.disc_next_ms  = now + 3000;
        break;
```

with:

```cpp
    case STATE_DISCONNECTED:
        e.disc_anim_start_ms = now;
        e.disc_age_ms        = 0;
        break;
```

- [ ] **Step 3: Delete `tick_disconnected` entirely**

In `src/eyes.cpp`, delete the whole `void tick_disconnected(EyesAnim& e, uint32_t now) { ... }` function (currently around lines 48–60). Nothing else references it after this change.

- [ ] **Step 4: Replace the `STATE_DISCONNECTED` branch in `eyes_tick`**

In `src/eyes.cpp`, replace:

```cpp
    case STATE_DISCONNECTED:
        tick_disconnected(e, now);
        e.draw_base_y = kBaseIdleY;
        e.draw_dx     = 0;
        if (e.disc_phase == 1) {
            e.draw_h = 30;
        } else {
            e.draw_h = 0;
        }
        break;
```

with:

```cpp
    case STATE_DISCONNECTED:
        e.disc_age_ms = now - e.disc_anim_start_ms;
        break;
```

The `draw_*` cached fields are not used by the new render path for this state.

- [ ] **Step 5: Update `eyes_reset` for the new fields**

In `src/eyes.cpp`, in `eyes_reset`, replace:

```cpp
    e.disc_phase           = 0;
    e.disc_next_ms         = now + 3000;
```

with:

```cpp
    e.disc_anim_start_ms   = now;
    e.disc_age_ms          = 0;
```

Leave every other line in `eyes_reset` unchanged.

- [ ] **Step 6: Rewrite the `STATE_DISCONNECTED` path in `eyes_render`**

In `src/eyes.cpp`, find the start of `eyes_render` (around line 162). It currently looks like:

```cpp
void eyes_render(Adafruit_ST7789& tft, const EyesAnim& e, BuddyState state) {
    tft.fillScreen(ST77XX_BLACK);

    if (state == STATE_DISCONNECTED && e.disc_phase == 0) {
        return;
    }

    if (state == STATE_WORKING) {
        // ... unchanged ...
    }

    int h = e.draw_h;
    if (h <= 0) {
        return;
    }

    uint16_t col = ST77XX_WHITE;
    if (state == STATE_DISCONNECTED && e.disc_phase == 1) {
        col = kDimGrey;
    }

    int16_t top = (int16_t)(e.draw_base_y + 15 - h / 2);
    tft.fillRect(kLeftX + e.draw_dx, top, kEyeW, h, col);
    tft.fillRect(kRightX + e.draw_dx, top, kEyeW, h, col);
}
```

Change it to:

```cpp
void eyes_render(Adafruit_ST7789& tft, const EyesAnim& e, BuddyState state) {
    tft.fillScreen(ST77XX_BLACK);

    if (state == STATE_DISCONNECTED) {
        const int cy  = kBaseIdleY + 15;       // 67
        const int top = cy - kLidH / 2;        // 62
        tft.fillRect(kLeftX,  top, kEyeW, kLidH, ST77XX_WHITE);
        tft.fillRect(kRightX, top, kEyeW, kLidH, ST77XX_WHITE);

        const uint32_t base = e.disc_age_ms % kZLoopMs;
        const uint32_t offsets[3] = {0, 1000, 2000};
        for (int i = 0; i < 3; i++) {
            uint32_t age = (base + offsets[i]) % kZLoopMs;  // 0..2999

            int x = kZSpawnX + (int)((int32_t)kZDriftX * (int32_t)age / (int32_t)kZLoopMs);
            int y = kZSpawnY + (int)((int32_t)kZDriftY * (int32_t)age / (int32_t)kZLoopMs);

            uint8_t  size;
            if      (age < 1000) size = 1;
            else if (age < 2000) size = 2;
            else                 size = 3;

            uint16_t col;
            if      (age < 1800) col = ST77XX_WHITE;
            else if (age < 2550) col = kDimGrey;
            else                 continue;  // last ~450 ms: don't draw

            tft.setCursor(x, y);
            tft.setTextSize(size);
            tft.setTextColor(col);
            tft.print('Z');
        }
        return;
    }

    if (state == STATE_WORKING) {
        // ... existing WORKING block, unchanged ...
    }

    int h = e.draw_h;
    if (h <= 0) {
        return;
    }

    int16_t top = (int16_t)(e.draw_base_y + 15 - h / 2);
    tft.fillRect(kLeftX + e.draw_dx, top, kEyeW, h, ST77XX_WHITE);
    tft.fillRect(kRightX + e.draw_dx, top, kEyeW, h, ST77XX_WHITE);
}
```

Notes for the engineer:
- The WORKING block (squint + sweat drop) is unchanged — leave it exactly as it is, just keep it where it sits in the function.
- The trailing dim-grey color logic (`if (state == STATE_DISCONNECTED && e.disc_phase == 1) { col = kDimGrey; }`) is no longer needed because DISCONNECTED returns early now. The fallthrough rect path always draws white.
- Don't forget the `return;` after the DISCONNECTED block — falling through into the WORKING/IDLE/WAITING path would double-draw.

- [ ] **Step 7: Build and confirm `eyes.cpp` compiles**

Run:
```bash
pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -40
```

Expected: build still FAILS, but only with errors in `main.cpp` referencing `disc_phase` (e.g. "no member named 'disc_phase' in 'EyesAnim'"). If you see errors in `eyes.cpp`, fix them before moving on.

---

## Task 3: Update the eyes-card dirty-check in `main.cpp`

**Files:**
- Modify: `src/main.cpp:41` (the static cache declaration)
- Modify: `src/main.cpp:413-429` (the eyes-card paint block)

- [ ] **Step 1: Rename the cache variable**

In `src/main.cpp`, find:

```cpp
static uint8_t      lastEyesDiscPhase = 0xFF;
```

(currently line 41) and replace it with:

```cpp
static uint32_t     lastEyesDiscAge = 0xFFFFFFFFu;
```

The sentinel `0xFFFFFFFFu` ensures the first comparison after boot is treated as "changed" and triggers an initial paint.

- [ ] **Step 2: Update the eyes-card dirty-check**

In `src/main.cpp`, find the `} else if (currentCard == CARD_EYES) {` block (currently around lines 413–429). It looks like:

```cpp
    } else if (currentCard == CARD_EYES) {
        eyes_tick(eyesAnim, currentState, now);
        bool eyesChanged = !eyesFrameValid ||
                           lastEyesState != currentState ||
                           lastEyesH != eyesAnim.draw_h ||
                           lastEyesDx != eyesAnim.draw_dx ||
                           lastEyesBaseY != eyesAnim.draw_base_y ||
                           lastEyesDiscPhase != eyesAnim.disc_phase;
        if (eyesChanged) {
            paint_current_card();
            lastEyesState = currentState;
            lastEyesH = eyesAnim.draw_h;
            lastEyesDx = eyesAnim.draw_dx;
            lastEyesBaseY = eyesAnim.draw_base_y;
            lastEyesDiscPhase = eyesAnim.disc_phase;
            eyesFrameValid = true;
        }
```

Replace with:

```cpp
    } else if (currentCard == CARD_EYES) {
        eyes_tick(eyesAnim, currentState, now);
        bool eyesChanged = !eyesFrameValid ||
                           lastEyesState != currentState ||
                           lastEyesH != eyesAnim.draw_h ||
                           lastEyesDx != eyesAnim.draw_dx ||
                           lastEyesBaseY != eyesAnim.draw_base_y ||
                           lastEyesDiscAge != eyesAnim.disc_age_ms;
        if (eyesChanged) {
            paint_current_card();
            lastEyesState = currentState;
            lastEyesH = eyesAnim.draw_h;
            lastEyesDx = eyesAnim.draw_dx;
            lastEyesBaseY = eyesAnim.draw_base_y;
            lastEyesDiscAge = eyesAnim.disc_age_ms;
            eyesFrameValid = true;
        }
```

This causes the eyes card to repaint every loop tick while `STATE_DISCONNECTED` is active (since `disc_age_ms` advances every tick). That matches what `WORKING` already does (its `draw_dx` is sine-driven and changes every tick).

- [ ] **Step 3: Build the firmware**

Run:
```bash
pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -20
```

Expected: SUCCESS. Output ends with `[SUCCESS] Took ... seconds`. If anything fails, the error will name the file and line — fix and re-run.

- [ ] **Step 4: Run the host test suite (no behavior change expected)**

Run:
```bash
pio test -e native 2>&1 | tail -20
```

Expected: all existing tests still pass (5 protocol parser + 6 state derivation = 11 tests). The eyes module is not host-tested, so nothing here exercises the new code — this is just a regression check.

- [ ] **Step 5: Commit**

```bash
git add src/eyes.h src/eyes.cpp src/main.cpp
git commit -m "$(cat <<'EOF'
feat(eyes): asleep look for STATE_DISCONNECTED

Replace the 3 sec blank + 200 ms grey eye blip with closed
eyelids (10x30 white rects) plus a continuous staggered trail
of three uppercase Z glyphs drifting diagonally up-right.

Eye animation state simplifies to disc_anim_start_ms +
cached disc_age_ms; main.cpp's eyes-card dirty-check switches
from disc_phase to disc_age_ms.

EOF
)"
```

---

## Task 4: Manual hardware verification

**Files:** none (verification only).

- [ ] **Step 1: Flash the device**

Run:
```bash
pio run -e adafruit_feather_esp32s3_reversetft -t upload
```

Expected: upload completes, board resets.

- [ ] **Step 2: Boot with no Claude Desktop connection**

Power-cycle the board (or just let it boot). Within ~30 seconds the device should be in `STATE_DISCONNECTED`.

Switch to the eyes card with the nav buttons (D0 / D2) until you see the eye animation card.

Expected:
- Two thick white horizontal bars at the eye positions (10 px tall, 30 px wide).
- A stream of uppercase `Z` glyphs spawning beside the right eye, drifting diagonally up-and-right, growing as they go, fading from white → grey → gone.
- One Z always rising, one mid-flight, one fading — never a gap.
- Footer (`OFFLINE` red badge + `Claude-XXXX`) remains visible.

- [ ] **Step 3: Connect to Claude Desktop**

Pair via Claude Desktop → Developer → Open Hardware Buddy → Connect.

Expected: Within one snapshot, the eyes card switches to the rectangle-eye `IDLE` look (or `WORKING`/`WAITING` depending on session state). No flicker, no leftover Z glyphs, no leftover eyelid bars.

- [ ] **Step 4: Disconnect and confirm return to asleep look**

Close Claude Desktop or disconnect. After up to 30 seconds (the LIVE_TIMEOUT_MS heartbeat window) the device returns to `STATE_DISCONNECTED`.

Expected: asleep look reappears cleanly. No leftover IDLE rectangles, no half-drawn Z glyphs.

- [ ] **Step 5: Mark done**

If steps 2–4 all pass, the feature is complete. If any step fails, capture what you saw (photo or description) and the failing condition, then revisit Task 2 / Task 3 — most likely candidates:
- Z glyphs in the wrong position → check `kZSpawnX`, `kZSpawnY`, `kZDriftX`, `kZDriftY`.
- Animation not advancing → check that `eyes_tick` updates `disc_age_ms`, and that `main.cpp`'s dirty-check compares `disc_age_ms`.
- Flicker on transition → check that `eyes_render` does the early `return;` after the DISCONNECTED block.

---

## Self-Review (completed by author)

**Spec coverage:** Eyelids ✓ (Task 2 step 6), Z trail ✓ (Task 2 step 6 + constants in step 1), removal of pulse ✓ (Task 2 steps 2–4), continuous-until-connected lifecycle ✓ (no fade in/out logic anywhere), main.cpp dirty-check ✓ (Task 3), other states untouched ✓ (only STATE_DISCONNECTED branches touched).

**Placeholder scan:** None found. Every step has either a complete code block or an exact command.

**Type consistency:** `disc_anim_start_ms` and `disc_age_ms` are `uint32_t` everywhere they appear (struct field, computation in `eyes_tick`, comparison in `main.cpp`). Cache variable in `main.cpp` is `uint32_t`. Constants `kZSpawnX`/`kZSpawnY`/`kZDriftX`/`kZDriftY` are `int`, `kZLoopMs` is `uint32_t`, `kLidH` is `int` — matches the pattern of existing constants (`kEyeW`, `kBaseIdleY`, `kDripMs`).
