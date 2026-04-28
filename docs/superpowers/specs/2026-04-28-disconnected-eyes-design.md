# Disconnected eyes — asleep look

## Goal

Replace the current `STATE_DISCONNECTED` animation (3 sec blank + 200 ms grey eye blip) with a personable "asleep" look: closed eyes plus a continuous Zzz trail drifting diagonally up-and-right, the entire time the device is disconnected.

## Scope

- Only `STATE_DISCONNECTED` rendering changes.
- `IDLE`, `WORKING`, `WAITING` are untouched.
- Footer (`OFFLINE` badge + device name) keeps rendering as today, on top of the asleep eyes.

## Visual

### Eyelids

- Two thick flat white horizontal lines.
- **10 px tall × 30 px wide**.
- Positioned at the existing eye x-coordinates: `kLeftX = 30`, `kRightX = 180`.
- Vertically centered on the IDLE eye centerline (`cy = kBaseIdleY + 15 = 67`), so `top = cy - 5 = 62`.
- Static — no pulse, no blink, no breathing.

### Zzz trail

- Three uppercase white `Z` glyphs in flight at all times, forming a continuous staggered trail.
- Each Z follows an identical trajectory:
  - **Spawn** near the upper-right of the right eye: `(x, y) = (210, 50)`.
  - **Drift** diagonally up-and-right: end position `(x + 20, y - 45) = (230, 5)`.
  - **Grow** from text size `1` → `2` → `3` over the trajectory (Adafruit GFX uses integer text scale; we step through three sizes rather than smoothly).
  - **Fade out** by stepping color over the trajectory: white → dim grey → not drawn. No explicit fade-in — the next Z covers that gap.
- Loop length: **3000 ms**.
- The three Zs are phase-offset by 1000 ms each (`t = 0`, `t = 1000`, `t = 2000`), so there's always one rising / one mid-flight / one fading.

## Behavior

- The asleep look is the **only** rendering for `STATE_DISCONNECTED`. The previous pulse behavior is removed entirely.
- Animation runs continuously the entire time the device is in `STATE_DISCONNECTED`. No fade-in on entry, no fade-out on exit — when we leave the state, the renderer for the new state takes over.
- The animation is derived directly from `now - disc_anim_start_ms`, where `disc_anim_start_ms` is captured the moment we enter `STATE_DISCONNECTED`. No per-Z state is required.

## Implementation

### `src/eyes.h`

Replace the existing DISCONNECTED-pulse fields in `EyesAnim`:

```cpp
// remove
uint8_t  disc_phase;
uint32_t disc_next_ms;

// add
uint32_t disc_anim_start_ms;  // millis() at entry to STATE_DISCONNECTED
uint32_t disc_age_ms;         // cached for render: (now - disc_anim_start_ms)
```

`disc_age_ms` is pre-computed in `eyes_tick` so `eyes_render` stays a pure function of `EyesAnim` (matches the existing pattern with `draw_dx`, `draw_h`, etc.).

`eyes_reset` zeros both fields. No other API changes — `eyes_reset`, `eyes_tick`, `eyes_render` keep their signatures.

### `src/eyes.cpp`

**Constants** (anonymous namespace, near existing eye constants):

```cpp
const int      kLidH       = 10;
const int      kZSpawnX    = 210;
const int      kZSpawnY    = 50;
const int      kZDriftX    = 20;
const int      kZDriftY    = -45;
const uint32_t kZLoopMs    = 3000;
```

**`arm_state(STATE_DISCONNECTED, now)`**: set `e.disc_anim_start_ms = now`. Drop the existing `disc_phase` / `disc_next_ms` setup.

**`tick_disconnected`**: delete the function entirely.

**`eyes_tick(state == STATE_DISCONNECTED)`** branch in the switch: replace the existing draw_h / draw_base_y / disc_phase logic with a single line caching the animation age:

```cpp
e.disc_age_ms = now - e.disc_anim_start_ms;
```

The `draw_*` fields are not used by the new render path for this state.

**`eyes_render(state == STATE_DISCONNECTED)`** branch (added before the existing fall-through `fillRect` path):

1. Draw two eyelid rects:
   ```cpp
   int cy = kBaseIdleY + 15;  // 67
   int top = cy - kLidH / 2;  // 62
   tft.fillRect(kLeftX,  top, kEyeW, kLidH, ST77XX_WHITE);
   tft.fillRect(kRightX, top, kEyeW, kLidH, ST77XX_WHITE);
   ```
2. Draw three Zs at phase offsets `0`, `1000`, `2000` ms:
   ```cpp
   uint32_t base = e.disc_age_ms % kZLoopMs;
   for (uint32_t off : {0u, 1000u, 2000u}) {
       uint32_t age = (base + off) % kZLoopMs;     // 0..2999
       int      x   = kZSpawnX + (int)(kZDriftX * (int)age / (int)kZLoopMs);
       int      y   = kZSpawnY + (int)(kZDriftY * (int)age / (int)kZLoopMs);
       uint8_t  size = (age < 1000) ? 1 : (age < 2000) ? 2 : 3;
       uint16_t col;
       if (age < 1800)      col = ST77XX_WHITE;
       else if (age < 2550) col = kDimGrey;
       else                 continue;  // last ~450 ms: don't draw
       tft.setCursor(x, y);
       tft.setTextSize(size);
       tft.setTextColor(col);
       tft.print('Z');
   }
   return;  // skip the rect-eye fall-through
   ```

## Tests

Existing host tests cover protocol parsing and state derivation. The eye renderer is not currently unit-tested, and this change is pure UI behavior on top of `Adafruit_ST7789`, so:

- No new unit tests required.
- Manual verification on hardware: power up the device, confirm the asleep look appears; pair to Claude Desktop and confirm the look transitions cleanly to `IDLE` (rectangle eyes) on connection; disconnect again and confirm we return to the asleep look.

## Out of scope

- Visual changes to `IDLE`, `WORKING`, `WAITING`.
- Adopting the upside-down-crescent eye shape from the reference image — that would be a separate, broader visual rewrite.
- Sound, brightness changes, or any non-rendering behavior changes on disconnect.
