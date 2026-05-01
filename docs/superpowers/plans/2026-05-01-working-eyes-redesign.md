# Working-state eyes redesign — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the `STATE_WORKING` look in `EyesCard` (currently `> <` squint + blue sweat drop + ±30 px scan) with a focused-thinking expression — 9 px furrowed slit eyes (rotated 15°), breathing height + slow gaze drift, rare blinks, and a 3-dot typing indicator above the right eye.

**Architecture:** Pure edit of two files: `src/ui/cards/EyesCard.h` and `src/ui/cards/EyesCard.cpp`. The new working state reuses the existing `scan_epoch_ms_` field as its time origin and adds a small set of new instance vars. Rendering uses two `fillTriangle` calls per eye (rotated parallelograms) and `fillCircle` for the dots, with per-frame partial erase to satisfy the no-`fillScreen` rule from `CLAUDE.md`. No new files, no new dependencies, no test infrastructure changes.

**Tech Stack:** C++17 / Arduino framework / Adafruit_GFX (`fillRect`, `fillTriangle`, `fillCircle`) / Adafruit_ST7789 / PlatformIO (`adafruit_feather_esp32s3_reversetft` env) / ESP32-S3.

**Spec:** `docs/superpowers/specs/2026-05-01-working-eyes-redesign.md` — read this first.

**TDD note:** This project has unit tests for pure logic (`test/test_state/`, `test/test_settings/`, etc.) but **none for `EyesCard`** — the existing IDLE blink, glance, and DISCONNECTED Z-animation are not covered by tests. The Adafruit ST7789 driver cannot be mocked cleanly, and the rendering math depends on visual correctness that no automated test catches. This plan follows the project's established pattern: incremental commits, each compiling cleanly, with on-device visual verification at the end. We do **not** add a new test suite for the animation math.

**Commit discipline:** Each task ends in one commit. Each commit must compile cleanly under `pio run` (the default `adafruit_feather_esp32s3_reversetft` env). Tasks are ordered so that intermediate states leave the old WORKING behavior intact until the new code is wired in — this means the old sweat-drop fields stay alive through Task 4 and only get removed in Task 5.

---

## File Structure

| File | Change |
|---|---|
| `src/ui/cards/EyesCard.h` | Modify — add new instance vars (Task 1), remove sweat vars (Task 5), declare `drawRotatedSlit` private helper (Task 3) |
| `src/ui/cards/EyesCard.cpp` | Modify — replace working-state constants (Task 2), implement helper (Task 3), replace `tick()`/`drawFrame()` working branches (Task 4), update `isDirty()`/`render()` carve-out + remove sweat refs (Task 5) |

No other files are touched.

---

## Task 1: Header — add new instance vars

**Files:**
- Modify: `src/ui/cards/EyesCard.h`

**Goal:** Add the new working-state instance variables alongside the existing ones. Don't remove the sweat vars yet — we need them so the unchanged `tick()`/`drawFrame()` continues to compile through this task.

- [ ] **Step 1: Open the header**

Read `src/ui/cards/EyesCard.h`. Confirm the existing private section currently has fields up to `last_disc_age_` (line 57 in the version this plan was written against).

- [ ] **Step 2: Add new instance variables**

Inside the `// Animation state.` block (just after `int8_t draw_sweat_y_;`), add the three new working-state animation vars:

```cpp
    // STATE_WORKING — rare blink, separate timer to avoid coupling with IDLE/WAITING blink_i_.
    uint32_t   next_work_blink_ms_;
    uint32_t   work_blink_step_deadline_ms_;
    int8_t     draw_work_blink_i_;        // -1 between blinks, else 0..kWorkBlinkN-1
    int8_t     draw_blink_h_;             // current blink-step height, or -1 when not blinking
    uint8_t    draw_dots_n_;              // typing-dots count, 0..3
```

Inside the `// Dirty-tracking ...` block (just after `int8_t last_sweat_y_;`), add:

```cpp
    int8_t     last_blink_h_;
    uint8_t    last_dots_n_;
```

Leave `draw_sweat_y_` and `last_sweat_y_` alone — they are removed in Task 5.

- [ ] **Step 3: Build to verify clean compile**

Run: `pio run -e adafruit_feather_esp32s3_reversetft`
Expected: SUCCESS. Warnings about the new unused fields are OK; errors are not.

- [ ] **Step 4: Commit**

```bash
git add src/ui/cards/EyesCard.h
git commit -m "$(cat <<'EOF'
refactor(eyes): add new STATE_WORKING animation/mirror fields

Adds working-state blink timer, blink-step index, current blink height,
typing-dots count, and the matching last_* mirrors. Sweat-drop fields are
left in place until the working-state rewrite lands so the build stays
clean across the intermediate commits.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Constants — add the new working-state constants

**Files:**
- Modify: `src/ui/cards/EyesCard.cpp` (top-of-file `namespace { … }` block)

**Goal:** Add new working constants. Leave the existing sweat constants in place for now (they are still referenced by the unchanged `drawFrame()`).

- [ ] **Step 1: Open `EyesCard.cpp` and locate the anonymous namespace**

The constants block starts at the top of the file (line 10 in the current code), with `kDimGrey`, `kSweatBlue`, `kEyeW`, etc.

- [ ] **Step 2: Append the new constants to the namespace**

Add this block **after** the existing `kBlinkH[] = …` array, before the closing `}  // namespace`:

```cpp
// ---- STATE_WORKING (focused-thinking redesign) ----
// All times in ms; angles in radians.
const uint32_t kWorkBreatheMs       = 1100;
const uint32_t kWorkDriftMs         = 1400;
const uint32_t kWorkDotsMs          = 1500;
const uint32_t kWorkBlinkIntervalMs = 7000;
const uint32_t kWorkBlinkStepMs     = 40;     // 7 steps × 40 ms = 280 ms total
const int      kWorkBaseH           = 9;
const int      kWorkBreatheAmp      = 2;      // h ranges 7..11
const int      kWorkDriftAmp        = 8;      // dx ranges -8..+8
const float    kWorkRotRad          = 0.2618f; // 15° in radians
const uint8_t  kWorkBlinkH[]        = {9, 6, 3, 0, 3, 6, 9};
const int      kWorkBlinkN          = 7;

const int      kWorkLeftCx          = 45;     // matches kLeftX  + kEyeW/2
const int      kWorkRightCx         = 195;    // matches kRightX + kEyeW/2
const int      kWorkEyeCy           = 67;     // matches kBaseIdleY + 15
const int      kDotsX[3]            = {184, 194, 204};
const int      kDotsY               = 22;
const int      kDotR                = 2;      // 5 px diameter via fillCircle r=2

// Rotation basis — precomputed; width is fixed at kEyeW.
const float    kCos15               = 0.9659258f;  // cosf(0.2618f)
const float    kSin15               = 0.2588190f;  // sinf(0.2618f)

// Per-frame erase rect for one eye (covers rotated 30×11 max bbox + ±drift).
const int      kWorkEraseW          = 52;     // = 32 (bbox) + 2*kWorkDriftAmp + 4 margin
const int      kWorkEraseH          = 17;     // = 30*sin15 + 11*cos15 + small margin

// Dots erase rect — covers all three positions (5 px dots).
const int      kDotsEraseX          = 180;
const int      kDotsEraseY          = kDotsY - 3;
const int      kDotsEraseW          = 30;
const int      kDotsEraseH          = 7;
```

- [ ] **Step 3: Build to verify clean compile**

Run: `pio run -e adafruit_feather_esp32s3_reversetft`
Expected: SUCCESS. New constants are unused — warnings about that are fine.

- [ ] **Step 4: Commit**

```bash
git add src/ui/cards/EyesCard.cpp
git commit -m "$(cat <<'EOF'
refactor(eyes): add STATE_WORKING redesign constants

Animation periods, slit geometry, rotation basis, and erase-rect sizes
for the focused-thinking working state. Sweat constants remain until the
rewrite lands.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Implement the rotated-slit drawing helper

**Files:**
- Modify: `src/ui/cards/EyesCard.h` (add private method declaration)
- Modify: `src/ui/cards/EyesCard.cpp` (add method definition)

**Goal:** A pure rendering helper that draws one rotated slit (parallelogram) of size 30 × `h`, centered at `(cx, cy)`, rotated by `+15°` if `sign == +1` and `−15°` if `sign == -1`. Implemented as 2 × `fillTriangle`.

- [ ] **Step 1: Add the declaration to `EyesCard.h`**

In the `private:` section, just below `void drawFrame(Adafruit_ST7789& tft, BuddyState state, bool full_clear);`, add:

```cpp
    void drawRotatedSlit(Adafruit_ST7789& tft, int cx, int cy, int h, int sign);
```

- [ ] **Step 2: Add the implementation to `EyesCard.cpp`**

Append this method definition at the **end** of the file (after `drawFrame`):

```cpp
void EyesCard::drawRotatedSlit(Adafruit_ST7789& tft, int cx, int cy, int h, int sign) {
    if (h <= 0) return;

    // Width-axis basis (constant; width = kEyeW). For sign = -1, negate the y component.
    const float ux = (kEyeW * 0.5f) * kCos15;
    const float uy = (kEyeW * 0.5f) * kSin15 * (float)sign;

    // Height-axis basis (per-frame because h varies). For sign = -1, negate the x component.
    const float halfH = h * 0.5f;
    const float vx = -halfH * kSin15 * (float)sign;
    const float vy =  halfH * kCos15;

    // Four corners (top-left, top-right, bottom-right, bottom-left).
    // R(+θ)·(±W/2, ±h/2) — y-axis points DOWN in screen coords, but the same
    // formula works because we treat (vx, vy) as the vertical-edge offset.
    const int16_t tlx = (int16_t)lroundf(cx - ux + vx);
    const int16_t tly = (int16_t)lroundf(cy - uy + vy);
    const int16_t trx = (int16_t)lroundf(cx + ux + vx);
    const int16_t try_ = (int16_t)lroundf(cy + uy + vy);
    const int16_t brx = (int16_t)lroundf(cx + ux - vx);
    const int16_t bry = (int16_t)lroundf(cy + uy - vy);
    const int16_t blx = (int16_t)lroundf(cx - ux - vx);
    const int16_t bly = (int16_t)lroundf(cy - uy - vy);

    // Two triangles split the parallelogram along the TL→BR diagonal.
    tft.fillTriangle(tlx, tly, trx, try_, brx, bry, ST77XX_WHITE);
    tft.fillTriangle(tlx, tly, brx, bry, blx, bly, ST77XX_WHITE);
}
```

- [ ] **Step 3: Build to verify clean compile**

Run: `pio run -e adafruit_feather_esp32s3_reversetft`
Expected: SUCCESS. Helper is unused; warnings about that are fine.

- [ ] **Step 4: Commit**

```bash
git add src/ui/cards/EyesCard.h src/ui/cards/EyesCard.cpp
git commit -m "$(cat <<'EOF'
feat(eyes): add drawRotatedSlit helper for furrowed working-state eye

Renders a 30×h parallelogram rotated ±15° via two fillTriangle calls.
Will be wired into STATE_WORKING in a follow-up commit.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Wire up the new tick() and drawFrame() WORKING branches

**Files:**
- Modify: `src/ui/cards/EyesCard.cpp` (`armState`, `tick`, `drawFrame`)

**Goal:** Replace the `STATE_WORKING` cases inside `armState`, `tick`, and `drawFrame` with the new logic. After this commit, the device renders the new look, but the now-dead sweat fields (`draw_sweat_y_`, `last_sweat_y_`) still exist in the header. They get removed in Task 5.

- [ ] **Step 1: Update `armState(STATE_WORKING, …)`**

Locate the `case STATE_WORKING:` branch in `armState` (~line 99 in current code). It currently reads:

```cpp
        case STATE_WORKING:
            scan_epoch_ms_ = now;
            break;
```

Replace it with:

```cpp
        case STATE_WORKING:
            scan_epoch_ms_              = now;
            next_work_blink_ms_         = now + kWorkBlinkIntervalMs;
            work_blink_step_deadline_ms_ = 0;
            draw_work_blink_i_          = -1;
            draw_blink_h_               = -1;
            draw_dots_n_                = 0;
            break;
```

- [ ] **Step 2: Replace the `case STATE_WORKING:` branch in `tick()`**

Locate the working branch (~line 167 in current code, inside `tick(uint32_t now_ms)`). It currently reads:

```cpp
        case STATE_WORKING: {
            float ph = (now_ms - scan_epoch_ms_) * (2.0f * 3.14159265f) / 1000.0f;
            draw_dx_     = (int16_t)(sinf(ph) * 30.0f);
            draw_base_y_ = kBaseIdleY;
            draw_h_      = 30;
            // Integer-only drip: 0→kDripSteps px over kDripMs, then snaps back.
            draw_sweat_y_ = (int8_t)((now_ms - scan_epoch_ms_) % kDripMs
                                     * kDripSteps / kDripMs);
            break;
        }
```

Replace the entire block with:

```cpp
        case STATE_WORKING: {
            const uint32_t t = now_ms - scan_epoch_ms_;
            const float    twoPi = 2.0f * 3.14159265f;

            // Slow horizontal gaze drift, 1.4 s period.
            draw_dx_ = (int16_t)lroundf(kWorkDriftAmp *
                          sinf(twoPi * (float)t / (float)kWorkDriftMs));

            // Breathing height, 1.1 s period — h ∈ [7, 11].
            const int breathe_h = kWorkBaseH +
                (int)lroundf(kWorkBreatheAmp *
                             sinf(twoPi * (float)t / (float)kWorkBreatheMs));

            // Rare blink, separate timer from IDLE/WAITING blink_i_.
            if (draw_work_blink_i_ >= 0) {
                if (now_ms >= work_blink_step_deadline_ms_) {
                    draw_work_blink_i_++;
                    if (draw_work_blink_i_ >= kWorkBlinkN) {
                        draw_work_blink_i_   = -1;
                        next_work_blink_ms_  = now_ms + kWorkBlinkIntervalMs;
                    } else {
                        work_blink_step_deadline_ms_ = now_ms + kWorkBlinkStepMs;
                    }
                }
            } else if (now_ms >= next_work_blink_ms_) {
                draw_work_blink_i_           = 0;
                work_blink_step_deadline_ms_ = now_ms + kWorkBlinkStepMs;
            }

            // Final eye height: blink overrides breathing if a blink is in progress.
            const bool blinking = (draw_work_blink_i_ >= 0);
            draw_h_       = blinking ? kWorkBlinkH[draw_work_blink_i_] : breathe_h;
            draw_blink_h_ = blinking ? (int8_t)kWorkBlinkH[draw_work_blink_i_] : (int8_t)-1;
            draw_base_y_  = kBaseIdleY;

            // Typing dots: 0..3 over 1.5 s (4 phases × 375 ms each).
            // Computed as (t / 375) % 4 — overflow-safe (kWorkDotsMs is exactly
            // divisible by 4, so this is the same value as (t * 4 / 1500) % 4
            // without the multiply that could overflow uint32_t at long t).
            draw_dots_n_ = (uint8_t)((t / (kWorkDotsMs / 4u)) % 4u);
            break;
        }
```

- [ ] **Step 3: Replace the `STATE_WORKING` block in `drawFrame()`**

Locate the working block (~line 261 in current code). It currently reads (the entire `if (state == STATE_WORKING) { … return; }` block):

```cpp
    if (state == STATE_WORKING) {
        // "> <" squint — left eye ">" (tip left), right eye "<" (tip right)
        int cy   = draw_base_y_ + 15;
        int half = kActionH / 2;
        int lx   = kLeftX  + draw_dx_;
        int rx   = kRightX + draw_dx_;
        tft.fillTriangle(lx,         cy,
                         lx + kEyeW, cy - half,
                         lx + kEyeW, cy + half, ST77XX_WHITE);
        tft.fillTriangle(rx,         cy - half,
                         rx,         cy + half,
                         rx + kEyeW, cy,        ST77XX_WHITE);
        // Sweat drop: triangle tip + circle body, drips kDripSteps px then resets.
        int ty = kSweatTipY + draw_sweat_y_;
        tft.fillTriangle(kSweatCX - 4, ty + 8,
                         kSweatCX + 4, ty + 8,
                         kSweatCX,     ty,      kSweatBlue);
        tft.fillCircle(kSweatCX, ty + 13, 5, kSweatBlue);
        return;
    }
```

Replace the entire block with:

```cpp
    if (state == STATE_WORKING) {
        // Per-frame partial erase — see CLAUDE.md "never fillScreen in continuous animations."
        // Erase one rect per eye (padded for drift) plus the dots region.
        if (!full_clear) {
            tft.fillRect(kWorkLeftCx  - kWorkEraseW / 2,
                         kWorkEyeCy   - kWorkEraseH / 2,
                         kWorkEraseW, kWorkEraseH, ST77XX_BLACK);
            tft.fillRect(kWorkRightCx - kWorkEraseW / 2,
                         kWorkEyeCy   - kWorkEraseH / 2,
                         kWorkEraseW, kWorkEraseH, ST77XX_BLACK);
            tft.fillRect(kDotsEraseX, kDotsEraseY,
                         kDotsEraseW, kDotsEraseH, ST77XX_BLACK);
        }
        // (When full_clear is true, render() has already done a single fillScreen.)

        // Furrowed slit eyes — left eye +15°, right eye -15°.
        drawRotatedSlit(tft, kWorkLeftCx  + draw_dx_, kWorkEyeCy, draw_h_, +1);
        drawRotatedSlit(tft, kWorkRightCx + draw_dx_, kWorkEyeCy, draw_h_, -1);

        // Typing dots — draw the leftmost draw_dots_n_ of three.
        for (uint8_t i = 0; i < draw_dots_n_; i++) {
            tft.fillCircle(kDotsX[i], kDotsY, kDotR, ST77XX_WHITE);
        }
        return;
    }
```

- [ ] **Step 4: Update `render()` so STATE_WORKING uses partial erase**

Find this block in `render()` (~line 200 in current code):

```cpp
    bool full_clear = stateJustChanged || (bs != STATE_DISCONNECTED);
```

Replace with:

```cpp
    bool full_clear = stateJustChanged ||
                      (bs != STATE_DISCONNECTED && bs != STATE_WORKING);
```

This propagates `full_clear=false` into the new working `drawFrame` block on every steady-state frame, triggering the partial-erase path.

- [ ] **Step 5: Build and confirm clean compile**

Run: `pio run -e adafruit_feather_esp32s3_reversetft`
Expected: SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add src/ui/cards/EyesCard.cpp
git commit -m "$(cat <<'EOF'
feat(eyes): replace STATE_WORKING with focused-thinking redesign

Eyes are 30x9 px slits rotated 15 degrees inward (furrowed). They
breathe (height 7-11 px @ 1.1 s), drift horizontally (+/- 8 px @ 1.4 s),
and blink rarely (every 7 s, 280 ms). A 3-dot typing indicator above
the right eye cycles 0->1->2->3 every 1.5 s. Frame loop uses partial
erase (per CLAUDE.md), no per-tick fillScreen.

Sweat-drop fields and IsDirty()/render() mirrors are still in place;
they are removed in the next commit.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Remove sweat-drop remnants and update isDirty()/mirror updates

**Files:**
- Modify: `src/ui/cards/EyesCard.h`
- Modify: `src/ui/cards/EyesCard.cpp` (constants block, `resetAnim`, `EyesCard()` ctor, `isDirty`, `render` mirror updates)

**Goal:** Delete the now-unused sweat-drop fields, constants, and dirty-tracking entries. Wire up the new dirty-tracking entries (`last_blink_h_`, `last_dots_n_`).

- [ ] **Step 1: Remove sweat constants from `EyesCard.cpp`**

In the anonymous namespace at the top of the file, delete these lines:

```cpp
const uint16_t kSweatBlue = 0x65FF;  // R≈99 G≈190 B≈255
…
const int      kSweatCX   = 195;  // x-centre of sweat drop (above right eye)
const int      kSweatTipY = 25;   // resting y of teardrop tip
const uint32_t kDripMs    = 2000; // drip cycle length in ms
const int      kDripSteps = 8;    // how many px it drops per cycle
```

Also delete `const int kActionH = 14;` since the `> <` squint that used it is gone. Leave `kEyeW`, `kLeftX`, `kRightX`, `kBaseIdleY`, `kBaseWaitY`, `kDimGrey`, and the Z-glyph constants — those are still used by IDLE / WAITING / DISCONNECTED.

- [ ] **Step 2: Remove sweat fields from `EyesCard.h`**

In the `// Animation state.` block, delete:

```cpp
    int8_t     draw_sweat_y_;
```

In the `// Dirty-tracking…` block, delete:

```cpp
    int8_t     last_sweat_y_;
```

- [ ] **Step 3: Remove sweat references from `resetAnim()` and the constructor**

In `EyesCard::EyesCard(const AppState&)`:

```cpp
    last_sweat_y_  = 0;
```

— delete this line.

In `EyesCard::resetAnim()`:

```cpp
    draw_sweat_y_            = 0;
```

— delete this line. Also add the new working-state mirror initializers right after the existing initializers (before the closing brace):

```cpp
    next_work_blink_ms_           = now;
    work_blink_step_deadline_ms_  = 0;
    draw_work_blink_i_            = -1;
    draw_blink_h_                 = -1;
    draw_dots_n_                  = 0;
```

And in the constructor (after the existing `last_*` initializers), add:

```cpp
    last_blink_h_  = -1;
    last_dots_n_   = 0;
```

- [ ] **Step 4: Update `isDirty()`**

The current `isDirty()` reads:

```cpp
bool EyesCard::isDirty() const {
    if (!frame_valid_) return true;
    if (last_state_    != state_.buddyState()) return true;
    if (last_h_        != draw_h_)            return true;
    if (last_dx_       != draw_dx_)           return true;
    if (last_base_y_   != draw_base_y_)       return true;
    if (last_sweat_y_  != draw_sweat_y_)      return true;
    if (last_disc_age_ != disc_age_ms_)       return true;
    return false;
}
```

Replace the `last_sweat_y_` line with the two new dirty checks:

```cpp
bool EyesCard::isDirty() const {
    if (!frame_valid_) return true;
    if (last_state_    != state_.buddyState()) return true;
    if (last_h_        != draw_h_)            return true;
    if (last_dx_       != draw_dx_)           return true;
    if (last_base_y_   != draw_base_y_)       return true;
    if (last_blink_h_  != draw_blink_h_)      return true;
    if (last_dots_n_   != draw_dots_n_)       return true;
    if (last_disc_age_ != disc_age_ms_)       return true;
    return false;
}
```

- [ ] **Step 5: Update mirror writes at the end of `render()`**

The current tail of `render()` reads:

```cpp
    last_state_    = bs;
    last_h_        = draw_h_;
    last_dx_       = draw_dx_;
    last_base_y_   = draw_base_y_;
    last_sweat_y_  = draw_sweat_y_;
    last_disc_age_ = disc_age_ms_;
    frame_valid_   = true;
```

Replace `last_sweat_y_  = draw_sweat_y_;` with two new mirror writes:

```cpp
    last_state_    = bs;
    last_h_        = draw_h_;
    last_dx_       = draw_dx_;
    last_base_y_   = draw_base_y_;
    last_blink_h_  = draw_blink_h_;
    last_dots_n_   = draw_dots_n_;
    last_disc_age_ = disc_age_ms_;
    frame_valid_   = true;
```

- [ ] **Step 6: Build and confirm clean compile**

Run: `pio run -e adafruit_feather_esp32s3_reversetft`
Expected: SUCCESS, with no warnings about unused `draw_sweat_y_` / `last_sweat_y_` (they're gone now).

- [ ] **Step 7: Commit**

```bash
git add src/ui/cards/EyesCard.h src/ui/cards/EyesCard.cpp
git commit -m "$(cat <<'EOF'
refactor(eyes): drop sweat-drop fields & wire new dirty checks

Removes draw_sweat_y_, last_sweat_y_, kSweatBlue, kSweatCX, kSweatTipY,
kDripMs, kDripSteps, kActionH. Adds last_blink_h_/last_dots_n_ checks
to isDirty() and the matching mirror writes at the end of render().

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: On-device verification

**Files:** none.

**Goal:** Flash the device and walk through every visual check in the spec's verification list. Address any issues with follow-up commits before declaring done.

- [ ] **Step 1: Flash to the device**

Plug in the Adafruit Feather ESP32-S3 ReverseTFT.
Run: `pio run -e adafruit_feather_esp32s3_reversetft -t upload`
Expected: upload succeeds, device reboots.

- [ ] **Step 2: Drive the device into STATE_WORKING**

Connect the device to its companion (Claude session) so it transitions out of DISCONNECTED, then trigger a working state (start a session that has running tasks). Use the card-navigation buttons (D0 / GPIO0 = next, D2 / GPIO2 = previous) to switch to the eyes card if it isn't already showing.

- [ ] **Step 3: Visually verify each item from the spec**

Hold the device in `STATE_WORKING` for at least 30 seconds and confirm:

- [ ] Eyes are tilted **inward** — inner ends down (`\  /` shape). If they look like `/  \` (raised), one of the rotation signs is wrong; flip the `sign` in the `drawRotatedSlit` calls in `drawFrame`.
- [ ] **No flicker.** No full-screen black flashes. If you see strobing, check that `render()` is passing `full_clear=false` into the working block (the carve-out edit in Task 4 Step 4 must have landed).
- [ ] **Breathing** is visible — the slit's height pulses gently 7↔11 px on a ~1.1 s rhythm.
- [ ] **Drift** is visible — both eyes slide together horizontally ±8 px on ~1.4 s.
- [ ] **Blink** fires roughly every 7 s and lasts ~280 ms. The eye briefly closes to 0 px and reopens.
- [ ] **Typing dots** above the right eye cycle: invisible → 1 dot → 2 dots → 3 dots → invisible, every 1.5 s. No leftover pixels.
- [ ] **No sweat drop residue.** The corner where the old blue teardrop appeared is pure black.

- [ ] **Step 4: Verify state transitions**

- [ ] Switch to STATE_IDLE (let Claude go quiet) and back to STATE_WORKING. Each transition should produce a single full-screen clear and then steady incremental frames — no doubled rendering, no leftover pixels from the previous state.
- [ ] Switch through STATE_WAITING (a permission-pending state, if reachable) and back. Same expectation.
- [ ] Disconnect the device from companion. STATE_DISCONNECTED Z-animation should be unchanged from before the redesign.

- [ ] **Step 5: If any check failed**

Stop. Each fix is its own diagnostic + commit:

- **Wrong rotation:** flip `sign` argument(s) in `drawFrame`'s `drawRotatedSlit` calls.
- **Flicker:** double-check `full_clear` carve-out (Task 4 Step 4) and the per-eye erase rect dimensions (`kWorkEraseW`, `kWorkEraseH`).
- **Blink doesn't fire:** confirm `next_work_blink_ms_` is initialized in `armState(STATE_WORKING)` and that `tick()` reaches the blink-scheduling block.
- **Dots stuck or skipping:** confirm `kDotsEraseW` covers all three dot positions (180–210 px) and that `draw_dots_n_` is updated in `tick()`.
- **Drift looks asymmetric:** likely a sign error in `drawRotatedSlit`'s `vx` for one of the eyes.

- [ ] **Step 6: Final commit (only if any fixes were needed in Step 5)**

If Step 5 produced fixes, commit them with a descriptive message — for example:

```bash
git add src/ui/cards/EyesCard.cpp
git commit -m "$(cat <<'EOF'
fix(eyes): correct working-state rotation sign so eyes furrow inward

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

If everything passed cleanly, no further commit is needed.

---

## Done

The redesigned `STATE_WORKING` look should now be live. Spec is satisfied:

- Furrowed 9 px slit, 15° rotation ✓ (Tasks 2, 3, 4)
- Breathing + drift + dots + rare blink animation ✓ (Task 4)
- Per-frame partial erase, no `fillScreen` in steady state ✓ (Task 4)
- Sweat drop and old `> <` squint removed ✓ (Tasks 4, 5)
- IDLE / WAITING / DISCONNECTED untouched ✓ (no edits to those branches in any task)
