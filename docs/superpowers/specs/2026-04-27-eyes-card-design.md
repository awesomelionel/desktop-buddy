# Eyes Card Design

## Goal

Add a second display card showing two animated retro pixel eyes that reflect the current device state (DISCONNECTED / IDLE / WORKING / WAITING). Navigate between the existing status card and the eyes card using the two hardware buttons.

## Display Spec

- Pixel block size: 10 px
- Each eye: 3×3 blocks = 30×30 px white square
- Gap between eyes: 12 blocks = 120 px
- Layout: both eyes horizontally and vertically centered on 240×135 black background
- Left eye X: `(240 - (30+120+30)) / 2` = 30 px from left edge
- Right eye X: 30 + 30 + 120 = 180 px from left edge
- Eye Y: `(135 - 30) / 2` = 52 px from top (before any vertical offset)

## State Animations

### DISCONNECTED
Eyes off (black screen). Brief dim pulse every ~3 s: eyes appear at ~10% brightness (ST7789 16-bit color `0x18C3` ≈ dark grey) for 200 ms then go black again. No blink, no movement.

### IDLE
- Eyes full white, centered.
- **Squish-blink** every ~8 s: eye height steps 30→20→10→0→10→20→(back to 30), 100 ms per step, center-aligned vertically. Total blink duration ~600 ms.
- **Glance** every ~7 s (random ±2 s): both eyes shift ±20 px horizontally for ~1 s then return to center. Direction chosen randomly each time.

### WORKING
- Eyes full white.
- **Horizontal scan**: eyes oscillate ±30 px horizontally (sinusoidal), ~1 s per full cycle (500 ms left, 500 ms right). No blink.

### WAITING
- Eyes full white, shifted 20 px upward from vertical center.
- Same squish-blink as IDLE.
- No horizontal movement.

## Button Navigation

- **D0 / GPIO0** — next card (INPUT_PULLUP, active low, 50 ms debounce)
- **D2 / GPIO2** — previous card (INPUT_PULLUP, active low, 50 ms debounce)
- Cards in order: `[CARD_STATUS, CARD_EYES]` (wraps around)
- On card switch: clear screen, render new card immediately

## Architecture

### New files
- `src/eyes.h` / `src/eyes.cpp` — eye animation state machine + render function
  - `struct EyesAnim` holds all animation timers (blink countdown, glance countdown, scan phase)
  - `void eyes_tick(EyesAnim&, BuddyState, uint32_t nowMs)` — advance timers, compute draw params
  - `void eyes_render(Adafruit_ST7789&, const EyesAnim&)` — clear screen + draw eyes

### Modified files
- `src/main.cpp`
  - Add `enum Card { CARD_STATUS, CARD_EYES }`
  - Add `static Card currentCard = CARD_STATUS`
  - Add button read + debounce (millis-based) for GPIO0 and GPIO2
  - Replace `delay(20)` with non-blocking millis delta loop
  - Dispatch `render()` vs `eyes_render()` based on `currentCard`
  - Pass `millis()` into `eyes_tick()` each loop iteration

## Implementation Notes

- Adafruit GFX has no alpha blending — the DISCONNECTED dim colour is a fixed 16-bit grey constant, not a fade.
- All animation timing driven by `millis()` delta; no `delay()` calls in the main loop after this change.
- Screen is fully cleared (`fillScreen(ST77XX_BLACK)`) before each eyes render, same pattern as the existing status render.
- No new library dependencies required.

## Out of Scope

- More than 2 cards
- Smooth inter-card transition animations
- Per-eye independent animation (both eyes always move together)
