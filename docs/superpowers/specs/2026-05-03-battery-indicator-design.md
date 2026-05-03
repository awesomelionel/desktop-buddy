# Battery indicator — design

Date: 2026-05-03
Status: Draft for review
Related: `src/ui/Footer.{h,cpp}` (extended), `src/ui/cards/EyesCard.{h,cpp}` /
`src/ui/cards/StatusCard.cpp` / `src/ui/cards/PromptCard.cpp` (callers of
`drawFooter`)

## Goal

Show the current battery state (charge percentage and charging state) on
every card so the user can see at a glance how much runtime is left.
Today the firmware ignores the battery entirely — there's no read of the
fuel gauge, no display, no warning when the battery is low. The user has
to plug the device into a charger or check the on-board LED to know the
battery state.

This is intentionally scoped to **display only**. No power-management
side effects (no aggressive sleep, no WiFi throttling at low charge); a
later spec can build on top if we want runtime estimates or
discharge-rate-driven warnings.

## Summary of what changes

1. **New `Battery` library** (`lib/battery/`) wraps the on-board MAX17048
   fuel gauge. Polls every ~10 s, exposes `present()` / `percent()` /
   `charging()`. Wraps Adafruit's `Adafruit_MAX1704X` library — no
   custom register-level code.

2. **Footer gains a battery indicator** at the right side of the existing
   18-px band, drawn by `ui::drawFooter`. Two new parameters: percent
   and charging-state. All callers pass them in.

3. **Animated charging fill.** When the battery is charging the icon's
   fill bar sweeps from 0 to the current level over 1500 ms and repeats.
   When not charging the fill is steady. The animation runs only inside
   the icon's 28 × 8 px fill region — no impact on the rest of the
   footer or the card body above it.

4. **Low-battery colour shift.** Below 20 % the icon outline, nub, and
   numeric all turn red. Hysteresis returns to white at ≥ 22 %.

5. **Graceful absence.** If the MAX17048 isn't detected at boot the
   indicator slot stays empty — no error UI, no logs. The rest of the
   footer renders unchanged.

## Architecture

```
┌─────────────────┐     I2C     ┌─────────────────────┐    ┌─────────────────┐
│  MAX17048       │ ◀────────── │  Battery (new)      │ ─▶ │  ui::drawFooter │
│  (built-in,     │             │  lib/battery/       │    │  (extended)     │
│   addr 0x36)    │             │                     │    └─────────────────┘
└─────────────────┘             │  begin()            │
                                │  tick(now_ms)       │
                                │  present() / pct()  │
                                │  charging()         │
                                └─────────────────────┘
                                          ▲
                                          │ ticked every loop iter
                                          │ (10 s I2C cadence inside)
                                          │
                                ┌─────────────────────┐
                                │  CardController     │
                                └─────────────────────┘
```

- **Single owner.** `CardController` owns the `Battery` instance, calls
  `tick()` from its `tick()`, and reads `pct()` / `charging()` to feed
  the per-card `setFooter()` paths (where they exist) and the direct
  `drawFooter()` calls in `StatusCard` / `PromptCard`.
- **State exposed read-only.** `Battery::pct()` and `charging()` return
  cached values updated at the 10-s I2C cadence.
- **No threads, no locks.** Single-threaded loop; `tick()` is called from
  the same thread that calls `pct()` / `charging()`.

## Visual layout (240 × 18 footer band)

Footer height stays at `kFooterH = 18` (unchanged from the WAITING
work). The new indicator sits on the right, leaving the LIVE pill and
device label untouched.

```
x:   0    4         38   44                       ~110              148    180  184  188             236  240
     ┌────┬─────────┬────┬─────────────────────────┬───────────────┬──────────┬─┬───────────────────┬───┐
y=0  │    │  LIVE   │    │ Claude-A1B2             │               │  ▓▓▓▓▓▓  │█│       87%         │   │ y=18
     └────┴─────────┴────┴─────────────────────────┴───────────────┴──────────┴─┴───────────────────┴───┘
       4   pill 34   gap   device label, ≤ 70 px      gap ≈ 38         icon 32       numeric (size 2)
                                                                       + nub 4       12 × 16 per char
```

| Element                  | x range          | Geometry                                  |
|--------------------------|------------------|-------------------------------------------|
| LIVE / OFFL pill         | 4 .. 37          | 34 × 12, vertically centred (unchanged)   |
| Device label             | 44 .. ≤ 110      | font size 1, vertically centred (unchanged) |
| Battery icon body        | 148 .. 179       | 32 × 12 px, 1-px outline                  |
| Battery icon nub         | 180 .. 183       | 4 × 6 px filled, vertically centred       |
| Battery fill bar         | 150 .. 177 (max) | up to 28 × 8 px, scaled to charge %       |
| Numeric (`87%` / `100%`) | right edge 235   | font size 2 → 12 × 16 px per char         |

The numeric is right-aligned to a fixed right edge so 3-char (`87%`) and
4-char (`100%`) values share the same slot. The icon's left edge is
fixed at x = 148 regardless of digit count.

### Colour palette

| Condition          | Outline + nub + numeric | Fill bar              |
|--------------------|--------------------------|-----------------------|
| Normal (pct > 20)  | white                    | white, steady at level |
| Low (pct ≤ 20)     | red (`ST77XX_RED` / 0xF800) | red, steady at level   |
| Charging, normal   | white                    | white, animated 0 → level |
| Charging, low      | red                      | red, animated 0 → level   |
| Battery absent     | (indicator hidden)       | (indicator hidden)     |

Charging fill colour matches the outline colour so a low battery on USB
clearly reads as "low" while the animation conveys "charging".

## Animation, cadence, hysteresis

### Charging animation

- Fired only when `charging() == true`.
- Period: **1500 ms**. Inside one period the fill width goes
  `0 → (pct/100) × 28` px linearly, holds for one frame, then resets.
- Linear (not eased) — the metaphor is "current flowing in", which feels
  mechanical, not biological.
- Per-frame cost: redraw only the inside-icon fill region (28 × 8 = 224
  px ≈ 50 µs over SPI). Implemented as a `fillRect(BLACK)` followed by a
  `fillRect(fill_color)` for the new width. The icon outline, nub, and
  numeric are not touched on animation frames.
- Tear-free without compositing tricks: the fill is one rect, so the LCD
  scanline only sees the interior change — never goes BLACK across the
  outline or numeric.

### When not charging

Fill is steady at `(pct/100) × 28` px wide. The fill region only repaints
when `pct` changes (every ~10 s at most), so no per-frame SPI cost. The
outline + nub + numeric repaint with the same gate.

### Read cadence

- `Battery::tick(now_ms)` is called every loop iteration but only hits
  I2C every **10 s**.
- The first poll fires **1 s** after `begin()` so the first frame after
  boot has a real value rather than 0 %.
- `charging()` is derived from MAX17048's CRATE register (% per hour):
  `charging = (CRATE > +0.5 %/hr)`. The ±0.5 %/hr dead band keeps a
  fully-charged battery (CRATE near zero) from oscillating between
  "charging" and "idle".

### Low-battery hysteresis

- Indicator goes red when `pct ≤ 20`.
- Returns to white when `pct ≥ 22`.
- 2-point gap prevents colour flicker on a battery hovering near the
  threshold.

## Code shape

### `lib/battery/Battery.h`

```cpp
#pragma once
#include <stdint.h>

class Adafruit_MAX17048;

class Battery {
public:
    Battery();
    ~Battery();

    // Probe the I2C bus for the MAX17048. Returns true on success;
    // false leaves the instance in a "not present" state forever.
    // Caller must have called Wire.begin() before calling this.
    bool begin();

    // Drive the periodic poll. Cheap when not yet due; reads the chip
    // every kPollIntervalMs ms (10 s) once due.
    void tick(uint32_t now_ms);

    // Cached state — safe to call any time.
    bool    present()  const;   // false until/unless begin() succeeded
    uint8_t percent()  const;   // 0..100; 0 if !present()
    bool    charging() const;   // false if !present()

private:
    Adafruit_MAX17048* chip_;   // owned; nullptr when !present()
    uint32_t           next_poll_ms_;
    uint8_t            cached_pct_;
    bool               cached_charging_;
    bool               low_state_;     // for hysteresis at the consumer
};
```

### `lib/battery/Battery.cpp`

Wraps `Adafruit_MAX17048` from PlatformIO lib registry
(`adafruit/Adafruit MAX1704X`). Adds the dependency to `platformio.ini`.

`begin()`:
1. `chip_ = new Adafruit_MAX17048;`
2. `if (!chip_->begin(&Wire)) { delete chip_; chip_ = nullptr; return false; }`
3. `next_poll_ms_ = millis() + 1000;`  // first poll 1 s in
4. return true.

`tick(now_ms)`:
1. If `!chip_` return.
2. If `(int32_t)(now_ms - next_poll_ms_) < 0` return.
3. `cached_pct_       = (uint8_t)constrain((int)chip_->cellPercent(), 0, 100);`
4. `cached_charging_  = (chip_->chargeRate() > 0.5f);`
5. `next_poll_ms_     = now_ms + 10000;`

`percent()` / `charging()` / `present()` are trivial getters.

### `src/ui/Footer.h` (extended)

```cpp
namespace ui {

constexpr int kFooterH    = 18;
constexpr int kFooterTopY = 117;
// ... existing constants ...

// Existing layout slots stay at their current positions.
constexpr int kBatteryIconX        = 148;
constexpr int kBatteryIconBodyW    = 32;
constexpr int kBatteryIconBodyH    = 12;
constexpr int kBatteryIconNubW     = 4;
constexpr int kBatteryIconNubH     = 6;
constexpr int kBatteryFillMaxW     = 28;     // body w − 2*margin
constexpr int kBatteryFillH        = 8;      // body h − 2*margin
constexpr int kBatteryFillInsetX   = 2;
constexpr int kBatteryFillInsetY   = 2;
constexpr int kBatteryNumericRightX = 235;   // right edge of "100%" / "87%"
constexpr int kBatteryLowThreshold  = 20;
constexpr int kBatteryLowRecover    = 22;

// Extended signature. Caller passes pct = 0xFF when the battery isn't
// present; the indicator is then skipped entirely. `low` is the
// already-debounced low-battery state (see Hysteresis location below)
// — the helper is stateless and just renders what it's told.
void drawFooter(Adafruit_ST7789& tft,
                const char* device_name, bool live,
                uint8_t pct, bool charging, bool low,
                uint32_t now_ms);

}  // namespace ui
```

`pct = 0xFF` sentinel keeps the API two args wider but doesn't introduce
a separate `pct_present` bool — the same arity is shared by the
"battery missing" and "battery present" cases. (`uint8_t` so the
sentinel is unmistakable from a real 0..100 value.)

### `src/ui/Footer.cpp`

`drawFooter` is split into three internal helpers for clarity:

```cpp
static void drawLivePill(tft, live);                  // existing logic
static void drawDeviceLabel(tft, device_name);        // existing logic
static void drawBatteryIndicator(tft, pct, charging,
                                 uint32_t now_ms);    // NEW
```

`drawBatteryIndicator(tft, pct, charging, now_ms)`:
1. If `pct == 0xFF` return.
2. `low = (pct <= kBatteryLowThreshold)` (the hysteresis is tracked at
   the consumer, see "Hysteresis location" below — the helper is
   stateless and just renders what it's told).
3. `color = low ? ST77XX_RED : ST77XX_WHITE;`
4. Draw outline rect (32 × 12) at (148, 120) using
   `tft.drawRect(..., color)`.
5. Draw nub (4 × 6) at (180, 123) using `tft.fillRect(..., color)`.
6. Compute fill width: animated when `charging`, steady otherwise.
   - Steady: `w = (pct * kBatteryFillMaxW) / 100`
   - Animated: `phase_ms = now_ms % 1500; t = phase_ms / 1500.0f;`
     `w = (uint8_t)((pct * kBatteryFillMaxW * t) / 100.0f);`
7. Erase the inside-icon rect to BLACK then draw the fill rect to
   `color` if `w > 0`.
8. Draw the numeric: format `"%u%%"` into a 5-char buffer, set text
   size 2, set text colour to `color`, set cursor at
   `(kBatteryNumericRightX - n_chars * 12, 118)`, print.

The animation is driven entirely by `now_ms` — no internal state in the
helper. Callers that want stable (non-animated) output when not charging
just pass `charging = false` and the helper renders a steady fill.

### `src/ui/CardController.{h,cpp}`

- Construct one `Battery battery_` member.
- In `begin()`: call `battery_.begin()` and log success/failure.
- In `tick(now_ms)`: call `battery_.tick(now_ms)` near the top.
- Cache the latest values for footer rendering:
  ```cpp
  uint8_t bat_pct = battery_.present() ? battery_.percent() : 0xFF;
  bool    bat_chg = battery_.charging();
  ```
- Pass these to `eyes_card_.setFooter(...)` (extending the existing
  signature) and to `prompt_card_.setFooter(...)`.

### `src/ui/cards/EyesCard.{h,cpp}` and `PromptCard.{h,cpp}`

- `setFooter(const char* device_name, bool live, uint8_t pct, bool charging)`
  in both. Stash `pct` and `charging` next to the existing
  `footer_device_` / `footer_live_` fields.
- The existing footer dirty-tracking in `EyesCard` (added during the
  WAITING work) extends to track `pct` and `charging`. The footer
  repaints when any of `(live, device_name, pct, charging)` differ from
  their snapshotted last values **or** when charging is currently true
  (the animation needs every-frame redraws of just the fill region).

  Because the charging animation only redraws the fill rect (not the
  whole footer), the per-frame cost stays around 50 µs even while
  charging.

### `src/ui/cards/StatusCard.cpp`

`StatusCard` already calls `ui::drawFooter` directly each render. Pass
the new args through. `StatusCard` doesn't track its own footer state —
its render is unconditional — so the charging animation works for free
when StatusCard is the visible card.

### Hysteresis location

The 20 / 22 hysteresis lives in `CardController`, not in `Battery` or
`drawFooter`:

```cpp
if (battery_.present()) {
    if (low_battery_) {
        if (battery_.percent() >= ui::kBatteryLowRecover) low_battery_ = false;
    } else {
        if (battery_.percent() <= ui::kBatteryLowThreshold) low_battery_ = true;
    }
}
```

`drawFooter` then receives the already-debounced state via the `bool low`
parameter — the helper itself stays stateless. The alternative (helper
owns the hysteresis state) would make `drawFooter` stateful, harder to
unit-test, and weird if multiple cards called it with the same battery
percent (their hysteresis would drift independently).

## Performance budget

| Operation                                        | Cost                |
|--------------------------------------------------|---------------------|
| `Battery::tick` no-op (most calls)               | ~1 cycle (timestamp compare) |
| `Battery::tick` poll (every 10 s)                | ~200 µs (2× I2C read) |
| `drawFooter` full repaint (footer state changed) | ~1 ms (existing) + ~50 µs battery slot |
| `drawFooter` charging-anim frame (just fill)     | ~50 µs              |

Running totals on a busy frame (WAITING with charging animation):
- WAITING render: ~2.5 ms (after the recent tearing fixes).
- Battery animation contribution: ~50 µs.
- Well within the 16 ms loop budget.

I2C bus impact: Wire is shared with whatever else uses I2C on the board
(currently nothing). The MAX17048 lives at 0x36 — no conflict with any
existing peripheral. The poll uses `Wire.beginTransmission/endTransmission`
which blocks for <1 ms; happens once per 10 s.

## Failure modes

| Scenario                                       | Behaviour                                                 |
|------------------------------------------------|-----------------------------------------------------------|
| MAX17048 not detected at boot                  | `present()` stays false; indicator slot is empty on every card. No log spam. |
| I2C bus error during a periodic poll           | Cached values stay at last-known; next poll retries. (Adafruit lib returns NaN/0 on failure — clamp to last value.) |
| Battery removed at runtime (LiPo unplugged)    | Device powers off (USB-only); irrelevant for runtime UI.   |
| USB plugged/unplugged                          | `charging()` flips at the next poll (≤ 10 s). Animation kicks in / out within one read cycle. |
| Battery reports pct > 100 (rare on the chip)   | Clamped to 100 in `Battery::tick`.                         |

## Testing

### Host tests (`pio test -e native`)

`Battery` is hard to host-test because it depends on Adafruit's MAX17048
library which depends on `Wire` / `Arduino.h`. Two options:

- **A. Skip host tests for `Battery`** — the logic that's worth testing
  (the 10-s gating, the CRATE → bool conversion) is a few lines and
  trivial. Test on-device instead.
- **B. Extract the logic into a pure-C++ helper** (`battery_logic.h`)
  with no Arduino deps, host-test it, and have `Battery` call into it.
  More setup, more correct.

Picking **A** for v1 — the surface is small enough that on-device
verification is enough. Add a B-style refactor only if/when the logic
grows more conditional behaviour.

### On-device verification

- Plug in USB on a partially-charged battery → indicator turns on,
  `charging()` becomes true within 10 s, fill bar starts the sweep
  animation.
- Unplug USB → animation stops, fill bar steady at current %.
- Run the device until pct drops to 20 → indicator turns red.
- Plug in USB while red → fill animates red, outline + numeric stay red
  until pct ≥ 22, then white again.
- Test with no battery (USB only) → MAX17048 still reports based on
  whatever it sees on its sense lines; expect the indicator to display
  ~100 % charging or some sensible value. Confirm no crash.

## Out of scope (deferred)

- **Time-remaining estimate.** The user picked "just show charge level"
  in Q1. A future spec can compute hours-remaining from CRATE × current
  pct.
- **Discharge profile per state.** Same — needs measurement of typical
  current draw in each state, then weighted estimation.
- **Power-saving side effects.** Low battery doesn't trigger reduced
  framerate, WiFi off, etc. Display only.
- **Battery health / cycle count.** Not tracked.
- **Charging current display (mA).** The MAX17048 doesn't measure
  current directly; it only reports rate of change of charge.
- **Voltage display.** User picked the icon + numeric form (Q3 option B)
  rather than the voltage form (option D).
- **Sleep behaviour.** The existing "sleep manager" only turns off the
  backlight; it doesn't actually deep-sleep the CPU. Out of scope here.

## Open questions

1. **`uint8_t pct = 0xFF` sentinel for "battery absent"** vs adding an
   explicit `bool present` parameter to `drawFooter`. The sentinel
   keeps the signature smaller; the bool is more explicit. Confirm
   which.
2. **Animation period (1500 ms)** — is the sweep speed right? You
   picked "animated" in Q4 but didn't specify cadence. 1500 ms is a
   common reference (matches iOS); easy to tune on the real device.
