# Permission Buttons — Design

**Status:** Draft
**Date:** 2026-04-27
**Scope:** Add an on-device permission decision UI driven by the three onboard buttons (D2 up, D0 down, D1 center). When Claude Desktop sends a snapshot containing a `prompt` object, the device shows a full-screen decision UI with three options — Approve, Deny, Dismiss — and writes the chosen decision back over the existing BLE Nordic UART Service.

## Goals & non-goals

**Goals**

- Let the user act on a Claude Desktop permission prompt without leaving the device.
- Map cleanly to the wire protocol in `cdb/REFERENCE.md` — only `"once"` and `"deny"` decisions exist; the device never invents new protocol verbs.
- Keep the existing display behavior unchanged when no prompt is pending.
- Keep new logic in pure, host-testable libraries (`pio test -e native`), matching the existing `lib/protocol` and `lib/state` pattern.

**Non-goals**

- No "Approve always" / persistent approval — the protocol has no such verb.
- No persistence across reboots (dismissed-id memory is RAM-only).
- No new BLE characteristics — we only start *using* the existing TX characteristic, which has been declared but unused.
- No auto-repeat on held buttons. No long-press shortcuts.
- No re-prompt retry logic on the device — if a write fails, the desktop's next snapshot will re-prompt.

## Behavior summary

Decisions made during brainstorming, in plain language:

| Decision | Choice |
| --- | --- |
| Options offered | **Approve / Deny / Dismiss** (three) |
| Dismiss semantics | **Sticky per `prompt.id`** — same id never re-shows; only a new id does |
| UI footprint | **Full-screen replacement**, footer (`LIVE` / device name) preserved |
| Buttons outside an active prompt | **Inert** |
| Stale prompt (field disappears or link goes OFFLINE) | **Auto-dismiss** the UI, drop any in-flight press |
| Default highlighted option | **Approve** |
| Layout | **Vertical stack**, no wrap-around |
| Confirmation feedback | **~500 ms flash** of `SENT: APPROVE` / `SENT: DENY` / `DISMISSED`, then UI hides |

## Architecture

Five units, each with one job. Pure-logic libraries have no Arduino dependencies and run under `pio test -e native`. Hardware-touching code stays in `src/`.

### `lib/buttons` — debouncer (new, host-testable)

Pure-logic debouncer. GPIO reading happens in `main.cpp`; this lib only consumes the resulting raw boolean per pin.

```c
enum ButtonEvent { BTN_NONE, BTN_UP, BTN_DOWN, BTN_CENTER };

struct Buttons {
    // per-pin: last-seen raw state, last-change timestamp, current debounced state
};

void         buttons_init(Buttons*);
ButtonEvent  buttons_step(Buttons*, uint32_t now_ms,
                          bool up_raw, bool down_raw, bool center_raw);
```

- A pin must be stable in its new state for **20 ms** before the debounced state changes.
- The library is polarity-agnostic — `up_raw`/`down_raw`/`center_raw` are normalized booleans where `true` means "pressed". The caller in `main.cpp` reads the GPIO and inverts where needed (see pin setup below).
- A press event is emitted on the **transition to pressed** of the debounced state.
- One event per call. If multiple pins are newly pressed in the same step, priority is **CENTER > DOWN > UP**.
- Holding a button does not auto-repeat — exactly one event per press.
- Held-at-boot pins (e.g., D0/BOOT) do **not** auto-fire: the first call's observed state is the starting state, no falling edge has been seen yet.

### `lib/protocol` — extended (existing, host-testable)

Add prompt parsing to the existing `protocol_parse_line`.

```c
struct ClaudePrompt {
    bool present;
    char id[24];
    char tool[16];
    char hint[64];
};

struct ClaudeStatus {
    // ...existing fields...
    ClaudePrompt prompt;
};
```

Parsing rules:

- A line is treated as a **snapshot** when its top-level JSON contains a `total` field (the existing parser already keys off this).
- On a snapshot **with** `prompt`: copy `id` / `tool` / `hint` into the fixed buffers (truncating safely), set `prompt.present = true`.
- On a snapshot **without** `prompt`: set `prompt.present = false`.
- On a non-snapshot line (acks, time-sync, owner): leave `prompt` untouched. This prevents an ack reply from accidentally clearing an active prompt.
- If `prompt` is present but missing `id`, or `id` would need to be truncated: set `prompt.present = false` and log to serial. Echoing back a truncated id would silently no-op on the desktop side.
- `hint` is sanitized on copy — `\r`/`\n` and any non-printable bytes are replaced with `?` so the wrap routine can operate on plain ASCII.

### `lib/prompt_ui` — selection state machine (new, host-testable)

Owns the prompt UI's logical state — visibility, highlighted option, dismissed-id memory, the flash timer, and the pending outgoing JSON line. No Arduino dependencies.

```c
enum PromptOption { OPT_APPROVE = 0, OPT_DENY, OPT_DISMISS };

struct PromptView {
    bool        visible;
    const char* tool;
    const char* hint;
    PromptOption highlight;
    const char* flash_text;   // null if not flashing
    uint16_t    flash_color;  // RGB565, only meaningful when flash_text != null
};

struct PromptUi {
    // visible, current id, highlight, last_dismissed_id,
    // flash_text, flash_deadline_ms, pending_outgoing[64], pending_outgoing_set
};

void prompt_ui_init  (PromptUi*);
void prompt_ui_update(PromptUi*, const ClaudePrompt&, bool live, uint32_t now_ms);
void prompt_ui_button(PromptUi*, ButtonEvent, uint32_t now_ms);
PromptView prompt_ui_view(const PromptUi*);
bool prompt_ui_take_outgoing(PromptUi*, char* buf, size_t buf_len);
```

State transitions:

- **Become visible** when `prompt.present && live && id != last_dismissed_id && !visible`. Highlight resets to `OPT_APPROVE`. Flash cleared.
- **Stay visible, no reset** when the same `id` arrives again on a new snapshot.
- **Replace** when a different `prompt.id` arrives while visible. Highlight resets to `OPT_APPROVE`. Flash cleared.
- **Auto-hide** when `prompt.present == false` *or* `live == false` while visible. Flash cleared. `last_dismissed_id` **not** updated (this is not a user dismiss).
- **Flash expiry**: on every `prompt_ui_update`, if visible and `flash_text` is set and `now >= flash_deadline_ms`, hide the UI and clear flash. (Hides 500 ms after press, regardless of whether the desktop has cleared `prompt`.)

Button handling (only when visible):

- `BTN_UP` → `highlight = max(0, highlight - 1)` (clamped, no wrap).
- `BTN_DOWN` → `highlight = min(2, highlight + 1)` (clamped).
- `BTN_CENTER`:
  - On `OPT_APPROVE`: write `{"cmd":"permission","id":"<id>","decision":"once"}` into `pending_outgoing`, set flash text `"SENT: APPROVE"` (green), flash deadline `now + 500`.
  - On `OPT_DENY`: write `{"cmd":"permission","id":"<id>","decision":"deny"}`, flash `"SENT: DENY"` (red).
  - On `OPT_DISMISS`: no JSON queued. Set `last_dismissed_id = current id`. Flash `"DISMISSED"` (yellow).

`prompt_ui_take_outgoing` fills `buf` from `pending_outgoing` and clears it; returns false if there's nothing pending. Holds at most one pending response.

### `src/ble_bridge` — extended

Add a TX writer.

```c
bool ble_write_line(const char* line);
```

- Appends `'\n'` and notifies on the existing TX characteristic (`6e400003-...`).
- Returns `false` if not connected or the notify queue is full. Caller does not retry — the desktop will re-prompt on its next snapshot if needed.

### `src/main.cpp` — wiring only

No business logic added here. Per loop iteration:

1. Read D2/D0/D1 with `digitalRead`, hand to `buttons_step`.
2. If a non-NONE event came back **and** `prompt_ui_view().visible`, call `prompt_ui_button`.
3. Drain BLE lines into the existing accumulator → `protocol_parse_line` (existing).
4. Call `prompt_ui_update(&ui, status.prompt, isLive(), millis())`.
5. If `prompt_ui_take_outgoing(...)` → `ble_write_line(...)` once.
6. Render: if `prompt_ui_view().visible` → `render_prompt(view)`; else existing `render()`.
7. Dirty-tracking is extended so we redraw when the prompt UI's view changes (visibility, highlight, flash text), not on every tick.

Pin setup at boot (per Adafruit Feather ESP32-S3 Reverse TFT pinout — see `cdb/src/hal/buttons.cpp`):

- **D0 (GPIO 0)** — BOOT button, **active LOW** with internal pull-up. `pinMode(0, INPUT_PULLUP)`. Pressed → `digitalRead(0) == LOW`. Used as **Down** in this feature.
- **D1 (GPIO 1)** — button, **active HIGH** with internal pull-down. `pinMode(1, INPUT_PULLDOWN)`. Pressed → `digitalRead(1) == HIGH`. Used as **Center**.
- **D2 (GPIO 2)** — button, **active HIGH** with internal pull-down. `pinMode(2, INPUT_PULLDOWN)`. Pressed → `digitalRead(2) == HIGH`. Used as **Up**.

`main.cpp` normalizes polarity before calling `buttons_step`:

```c
bool up_raw     = digitalRead(2) == HIGH;
bool down_raw   = digitalRead(0) == LOW;
bool center_raw = digitalRead(1) == HIGH;
ButtonEvent ev  = buttons_step(&btns, millis(), up_raw, down_raw, center_raw);
```

Then `buttons_init(&btns)` once at boot.

## Data flow — one decision, end to end

1. Desktop sends a snapshot with `prompt:{id,tool,hint}`. The line accumulator hands it to `protocol_parse_line`, which fills `status.prompt`. `lastSnapshotMs = millis()`.
2. `prompt_ui_update` becomes visible (id ≠ last dismissed, live=true). Highlight = APPROVE.
3. `main.cpp` renders the prompt screen.
4. User presses Down → `buttons_step` returns `BTN_DOWN` → `prompt_ui_button` moves highlight to DENY. Re-render.
5. User presses Center → `prompt_ui_button` queues `{"cmd":"permission","id":"<id>","decision":"deny"}` into `pending_outgoing`, sets flash `"SENT: DENY"` for 500 ms.
6. `main.cpp` calls `prompt_ui_take_outgoing(buf, sizeof buf)` → `ble_write_line(buf)`. Frame renders with the flash text replacing the options stack.
7. 500 ms later, `prompt_ui_update` sees `now >= flash_deadline_ms` → hides UI. Next render falls back to the normal screen.
8. Desktop processes the decision and the next snapshot drops `prompt`. (We've already hidden the UI by this point; this is just confirmation.)

## UI layout

Screen is 240×135 landscape. Adafruit GFX default font is 6×8 px per cell at size 1; size 2 is 12×16 px. Footer at y=118 is reused unchanged.

```
y=0    ┌──────────────────────────────────┐
        │           PERMISSION?            │   header, size 2 white, centered
y=2     │                                  │     occupies y=2..18
y=18    │                                  │
        │  Tool: Bash                      │   size 1 cyan,  x=8, y=28
y=28    │  rm -rf /tmp/foo                 │   size 1 white, x=8, y=40
y=40    │  (continued if hint > 38 chars)  │            ", x=8, y=50, max 2 lines
y=58    │                                  │
        │   ▶ Approve                      │   size 2, vertical stack — y=66..114
y=66    │     Deny                         │     16 px row height, no inter-row gap
y=82    │     Dismiss                      │     highlighted: filled white bg,
y=98    │                                  │       black text, "▶ " prefix at x=8
        ├──────────────────────────────────┤     non-highlighted: black bg, white
y=118   │ LIVE  Claude-A1B2                │       text, indent x=24 to align
        └──────────────────────────────────┘     existing footer, unchanged
y=135
```

- **Header.** `"PERMISSION?"` size 2 white at y=2, centered horizontally via `getTextBounds`.
- **Tool/hint area.** `Tool: <tool>` size 1 cyan at x=8, y=28, truncated to fit one line. `<hint>` size 1 white wrapped to ~38 chars per line, max 2 lines, at y=40 and y=50. Empty hint → lines skipped.
- **Options stack.** Three rows at y=66, y=82, y=98 (size 2 = 16 px tall, no inter-row gap). The bottom row ends at y=114, leaving 4 px before the footer at y=118. Each row is 240 px wide. Highlighted row: filled white rectangle background, black text, `"▶ "` prefix starting at x=8. Non-highlighted rows: black background, white text, no prefix, indent x=24 to keep the text origin aligned with the highlighted row's text origin.
- **Flash state.** Replaces all three option rows with one centered string at y=82 (the middle row's y), size 2: `"SENT: APPROVE"` (green), `"SENT: DENY"` (red), or `"DISMISSED"` (yellow). Header/tool/hint/footer unchanged. Hidden after 500 ms.
- **Footer.** Identical to existing screen — `LIVE`/`OFFLN` colored at x=8 y=118, then device name in white.

## Edge cases

- **Button bounce.** Stable-low for 20 ms required before the debounced state flips; events fire on the falling edge of debounced state, not raw.
- **Two buttons pressed simultaneously.** Resolved CENTER > DOWN > UP. Confirm wins over an accidental nav press alongside it.
- **Held at boot.** Debouncer's first call records observed state as starting state; no event fires until release-then-press.
- **GPIO floating.** Pull-ups configured at init; failure mode is "no events emitted", not spurious presses.
- **Snapshot missing `prompt`.** `present=false`. UI hides if visible.
- **Snapshot prompt missing `id` or id too long.** `present=false`, log to serial. UI does not open.
- **Same `prompt.id` re-arrives.** No state reset. If previously dismissed, stays hidden.
- **New `prompt.id` while a different one is on screen.** Replace: switch id, reset highlight to APPROVE, clear flash.
- **`last_dismissed_id` persistence.** RAM only. Cleared on reboot; desktop will only re-send a still-pending prompt anyway.
- **Press-then-disconnect race.** `ble_write_line` returns false → response dropped. Desktop re-prompts on next snapshot.
- **Notify queue full.** Same as above — drop, no retry.
- **Disconnect mid-flash.** F1 hides UI immediately; flash cut short. The `OFFLN` revert is itself meaningful feedback.
- **Snapshot drops `prompt` mid-flash.** F1 hides immediately; flash cut short.
- **Hint with control bytes / newlines.** Sanitized to `?` on copy in `protocol_parse_line`.
- **Tool name longer than the line.** Truncated at render with `printf("%.*s", n, ...)`.
- **No decision queueing.** `pending_outgoing` holds one entry. A second press before drain overwrites; drain happens on the next loop tick (~20 ms), so unreachable in practice.

## Memory & resource impact

- `ClaudePrompt` adds ~105 bytes to `ClaudeStatus`.
- `PromptUi` state ≈ 64 bytes (id + dismissed_id + view fields + 64-byte outgoing buffer).
- `Buttons` state ≈ 24 bytes.
- Total additional static RAM: < 200 bytes. No heap.
- No new BLE characteristics. No new task / no new ISR. Same 20 ms loop tick.

## Testing

### Host tests (`pio test -e native`)

**`test/test_buttons/`** — new

- `test_no_event_on_high_pins`
- `test_debounce_rejects_short_blip` (5 ms low pulse → no event)
- `test_emits_press_on_stable_low` (20 ms low → one event)
- `test_no_repeat_while_held` (200 ms low → exactly one event)
- `test_re_arms_after_release` (low/high/low → two events)
- `test_priority_when_simultaneous` (UP+DOWN+CENTER → CENTER, others consumed)
- `test_held_at_boot_does_not_fire` (initial low → no event until release+press)

**`test/test_protocol/`** — extend

- `test_parse_prompt_full`
- `test_parse_prompt_absent_clears`
- `test_parse_ack_does_not_clear_prompt`
- `test_parse_prompt_missing_id_drops`
- `test_parse_prompt_id_truncation_drops`
- `test_parse_prompt_hint_unprintable_sanitized`

**`test/test_prompt_ui/`** — new

- `test_hidden_until_prompt_present`
- `test_shows_with_default_approve`
- `test_up_down_navigation_clamped`
- `test_center_on_approve_emits_once_json`
- `test_center_on_deny_emits_deny_json`
- `test_center_on_dismiss_emits_nothing`
- `test_dismiss_is_sticky_per_id`
- `test_auto_hide_when_prompt_disappears`
- `test_auto_hide_when_offline`
- `test_new_id_replaces_visible_prompt`
- `test_flash_clears_after_500ms`
- `test_press_while_hidden_is_noop`

### On-device manual checklist

1. Trigger a permission prompt from desktop → UI appears with Approve highlighted.
2. Up/down moves highlight, no wrap.
3. Approve → desktop reports tool ran; `SENT: APPROVE` flashes ~500 ms.
4. Deny → desktop reports rejection; `SENT: DENY` flashes.
5. Dismiss → UI clears, desktop still pending; same id does not re-show; new id does.
6. Disconnect during prompt → UI clears immediately.
7. Press buttons in IDLE → no flicker, no serial errors.

## File changes summary

```
lib/buttons/buttons.h              (new)
lib/buttons/buttons.cpp            (new)
lib/protocol/protocol.h            (extended — ClaudePrompt struct on ClaudeStatus)
lib/protocol/protocol.cpp          (extended — parse `prompt` object, sanitize hint)
lib/prompt_ui/prompt_ui.h          (new)
lib/prompt_ui/prompt_ui.cpp        (new)
src/ble_bridge.h                   (extended — ble_write_line)
src/ble_bridge.cpp                 (extended — TX notify path)
src/main.cpp                       (extended — pin init, button poll, ui wiring,
                                    render_prompt branch)
test/test_buttons/                 (new)
test/test_protocol/                (extend)
test/test_prompt_ui/               (new)
README.md                          (already updated as part of this brainstorm)
```
