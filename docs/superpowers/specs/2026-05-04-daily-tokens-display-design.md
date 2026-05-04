# Daily token count on the StatusCard — design

Date: 2026-05-04
Status: Draft for review
Related: `lib/protocol/protocol.{h,cpp}` (parser extension),
`src/ui/cards/StatusCard.{h,cpp}` (display extension), `cdb/REFERENCE.md`
(wire protocol)

## Goal

Show the user how many output tokens have been generated today, prominently
on the StatusCard. The wire protocol from Claude Desktop already carries
`tokens_today` (output tokens since local midnight, persisted across
desktop restarts — see `cdb/REFERENCE.md` line 70), but the firmware's
parser ignores the field and the StatusCard doesn't display it.

This is intentionally narrow: only `tokens_today` from the existing
snapshot stream is parsed. No history graph, no day-over-day comparison,
no cumulative `tokens` field.

## Summary of what changes

1. **Protocol parser learns one new field.** `ClaudeStatus` gains a
   `tokens_today` member; `protocol_parse_line` reads it from incoming
   snapshots and preserves the previous value when the field is absent
   (matching the existing partial-update semantics).

2. **StatusCard renders a new prominent line.** Below the counters and
   above the message text, in font size 2 (the second-largest size used
   on this card after the state name), white text, centred:
   `31.2K tokens today` / `523 tokens today` / `1.0M tokens today`. Hidden
   entirely when no snapshot has arrived yet.

3. **Number formatting helper.** A small `format_token_count(uint32_t,
   char* buf, size_t buf_len)` utility produces the abbreviated string
   (`523`, `31.2K`, `1.0M`). Lives next to other display utilities.

## Protocol layer

### `ClaudeStatus` extension

Add one field to `lib/protocol/protocol.h`:

```cpp
struct ClaudeStatus {
    uint8_t      total;
    uint8_t      running;
    uint8_t      waiting;
    char         msg[32];
    bool         valid;
    ClaudePrompt prompt;
    uint32_t     tokens_today;   // NEW — output tokens since local midnight
};
```

`uint32_t` is enough headroom: 4.29 billion tokens, well past any
realistic daily count (a million tokens is already an extreme power-user
day).

### Parser extension

In `lib/protocol/protocol.cpp::protocol_parse_line`, after the existing
field reads, add:

```cpp
if (doc["tokens_today"].is<uint32_t>()) {
    out->tokens_today = doc["tokens_today"].as<uint32_t>();
}
// else: leave previous value unchanged (matches partial-update semantics)
```

`tokens_today` is **not** considered when the parser sets `out->valid =
true` — it's optional from the bridge's perspective, the same way the
`prompt` block is.

The cumulative `tokens` field on the wire is also ignored for now (out
of scope — see Out of scope below).

### Backward compatibility

- Snapshots without `tokens_today`: previous value retained, defaults to
  `0` from initial `memset`-style zero-init.
- Snapshots with `tokens_today: 0` after activity: that's a real reset
  (e.g. clock crossed midnight) and we honour it.
- Negative or absurdly large values: ArduinoJson's `as<uint32_t>` will
  silently coerce; values above `UINT32_MAX` or negative are unlikely in
  practice. No clamping needed.

## Display layer

### StatusCard layout (240 × 135)

```
y=  0 ┌────────────────────────────────────────┐
y= 20 │           IDLE                          │  state name (size 3, white) — unchanged
y= 58 │   total 3   run 1   wait 0              │  counters (size 1, cyan) — moved up from y=62
y= 70 │       31.2K tokens today                │  NEW — daily token line (size 2, white, centred)
y= 92 │   approve: Bash                         │  message line 1 (size 1, white) — moved down from y=80
y=104 │   ls -la                                │  message line 2 (size 1, white) — moved down from y=92
y=117 │  [LIVE] Claude-A1B2          [bat]      │  footer (kFooterH = 18) — unchanged
y=135 └────────────────────────────────────────┘
```

| Element       | y change         | Reason                                      |
|---------------|------------------|---------------------------------------------|
| State name    | none (y=20)      | The card's headline — keep anchored.        |
| Counters      | 62 → 58 (-4)     | Tighten the gap before the new token line.  |
| Token line    | NEW at y=70      | Below counters, font size 2 (16 px tall).   |
| Message ln 1  | 80 → 92 (+12)    | Make room for the new size-2 line.          |
| Message ln 2  | 92 → 104 (+12)   | Same.                                       |
| Footer        | none (y=117)     | Untouched.                                  |

### Token line rendering rules

- **Font:** Adafruit GFX text size 2 → 12 × 16 px per character.
- **Colour:** `ST77XX_WHITE` (matches the state name; the cyan counters
  stay differentiated as auxiliary numeric data).
- **Format:** `<formatted-count> tokens today`. Always centred
  horizontally on the screen (computed from the rendered string width
  via `tft.getTextBounds`).
- **Hidden when:** `state_.status().valid == false` (no snapshot received
  yet — don't show "0 tokens today" before we know).
- **Shown as `0 tokens today`** when valid but `tokens_today == 0` (real
  zero — quiet day, post-midnight reset, etc.). Keeps the layout stable.

### Number formatting

A small free function in a new header `lib/protocol/format.h` (or
inlined in `StatusCard.cpp` as a static; see "Code shape" below):

```cpp
// Render `n` into `buf` as a compact human-readable string.
//   n <      1 000  →  "523"
//   n <  1 000 000  →  "31.2K"  (one decimal if 9.95K..999K, else one decimal until 99.9K)
//   n >= 1 000 000  →  "1.0M"   (one decimal up to 9.99M, then "10M" with no decimal)
//
// `buf_len` must be >= 8 (worst case "999.9K" = 6 chars + NUL + 1 spare).
// Returns the number of chars written (excluding NUL), or 0 on error.
size_t format_token_count(uint32_t n, char* buf, size_t buf_len);
```

Format rules — boundaries chosen so rounding never causes a value to
"jump" between two format widths:

| Range                          | Format         | Examples                          |
|--------------------------------|----------------|-----------------------------------|
| `n < 1 000`                    | integer        | `0`, `7`, `523`                   |
| `1 000 ≤ n < 99 500`           | one decimal, K | `1.0K`, `9.9K`, `31.2K`, `99.4K`  |
| `99 500 ≤ n < 999 500`         | integer, K     | `100K`, `523K`, `999K`            |
| `999 500 ≤ n < 9 950 000`      | one decimal, M | `1.0M`, `4.3M`, `9.9M`            |
| `n ≥ 9 950 000`                | integer, M     | `10M`, `42M`                      |

Rounding is half-up to the nearest displayed unit. Boundaries pre-empt
the round-up: `99 499` → `99.5K`, `99 500` → `100K`; `999 499` → `999K`,
`999 500` → `1.0M`. That keeps the rendered string from ever spuriously
crossing a width boundary.

Worst-case rendered width: `99.4K` or `9.9M` = 4 chars; numbers with 5
chars don't occur (`100K`/`999K`/`10M`/`42M` are all ≤ 4). Combined with
`" tokens today"` (13 chars), full string ≤ 17 chars × 12 px = 204 px —
fits with ~18 px margin each side.

### Dirty tracking

`StatusCard::isDirty()` already tracks `state_.buddyState()`,
`status.msg`, and `live`. Add tracking for `tokens_today`:

```cpp
if (last_drawn_tokens_today_ != state_.status().tokens_today) return true;
```

And snapshot it after rendering. The token count typically only changes
when a snapshot arrives — every few seconds at most — so per-frame impact
is nil.

## Code shape

### `lib/protocol/protocol.h`

Add `uint32_t tokens_today;` to the `ClaudeStatus` struct (one new line).

### `lib/protocol/protocol.cpp`

Add the field read inside `protocol_parse_line`, immediately after the
existing optional-field reads (e.g. after `running`/`waiting`):

```cpp
if (doc["tokens_today"].is<uint32_t>()) {
    out->tokens_today = doc["tokens_today"].as<uint32_t>();
}
```

### `lib/protocol/format.h` and `format.cpp`

Two new files implementing `format_token_count`. Lives in
`lib/protocol/` because the formatting is paired with the protocol
field semantics; this keeps the `format.h` reusable from any consumer
that wants to render the count (StatusCard today; potentially others
later).

### `src/ui/cards/StatusCard.h`

Add tracking field:

```cpp
uint32_t last_drawn_tokens_today_;
```

Initialize to `0xFFFFFFFFu` in the constructor so the first render
always paints (matching the `last_disc_age_` pattern in `EyesCard`).

### `src/ui/cards/StatusCard.cpp`

In `render()`, after the counters block and before the message block:

```cpp
if (status.valid) {
    char tok_buf[8];
    format_token_count(status.tokens_today, tok_buf, sizeof(tok_buf));
    char line[32];
    snprintf(line, sizeof(line), "%s tokens today", tok_buf);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    int16_t x1, y1; uint16_t tw, th;
    tft.getTextBounds(line, 0, 0, &x1, &y1, &tw, &th);
    tft.setCursor((display.width() - (int)tw) / 2, 70);
    tft.print(line);
}
```

Move the `setCursor(8, 62)` for the counters block to `setCursor(8, 58)`,
and the message block's `setCursor(8, 80)` / `setCursor(8, 92)` to
`setCursor(8, 92)` / `setCursor(8, 104)`.

Update `isDirty()` and the trailing snapshot block as described above.

### `test/test_protocol/test_protocol.cpp`

Add tests:

- Snapshot with `"tokens_today": 31200` → `out.tokens_today == 31200`.
- Snapshot without `tokens_today` → previous value retained.
- Snapshot with `"tokens_today": 0` after a non-zero value → `out.tokens_today == 0` (midnight reset honoured).
- Snapshot with malformed `tokens_today` (string instead of number) → previous value retained.

### `test/test_format/test_format.cpp` (new test suite)

- `0` → `"0"`
- `7` → `"7"`
- `999` → `"999"`
- `1000` → `"1.0K"`
- `9949` → `"9.9K"`     (just below the half-K rounding point)
- `31 200` → `"31.2K"`
- `99 499` → `"99.5K"`  (last value with the K-decimal format)
- `99 500` → `"100K"`   (boundary: integer K kicks in)
- `523 000` → `"523K"`
- `999 499` → `"999K"`  (last value with the integer-K format)
- `999 500` → `"1.0M"`  (boundary: M kicks in)
- `1 000 000` → `"1.0M"`
- `4 321 000` → `"4.3M"`
- `9 949 999` → `"9.9M"`
- `9 950 000` → `"10M"`  (boundary: integer M kicks in)

Buffer-too-small case: `format_token_count(31200, buf, 4)` → returns 0
and leaves `buf[0] = 0`.

## Failure modes

| Scenario                                              | Behaviour                                                         |
|-------------------------------------------------------|-------------------------------------------------------------------|
| Bridge omits `tokens_today` from snapshots entirely   | Field stays at its last-seen value (or 0 if never seen). Line still renders.           |
| Bridge sends `tokens_today: -1` or non-numeric        | ArduinoJson coerces to 0 / previous value retained. No crash.     |
| Daily count crosses 1M mid-day                         | Format flips to `1.0M` next render. The line width may shift by a few px (recentred each render) — no layout breakage. |
| `status.valid == false` (no snapshot ever arrived)    | Token line not drawn. Counters and message blocks render in their new positions; the token line slot stays blank. |

## Performance budget

- Parser: one extra `doc["tokens_today"].is<uint32_t>()` check + one
  assign. Negligible (< 5 µs).
- Render: one `format_token_count` call (~10 µs) + one `getTextBounds`
  (~50 µs) + one text print (~600 µs at size 2). Total per StatusCard
  render ≈ 1 ms — well under the existing render budget. Only happens
  when `isDirty()` flips, not every frame.

## Testing

- Host tests for parser and formatter as described above.
- On-device verification:
  - Pair with Claude Desktop. Confirm snapshots include `tokens_today`.
  - Generate some tokens; confirm the line updates.
  - Cross local midnight (or simulate by injecting a snapshot with
    `tokens_today: 0`); confirm the display drops back to `0 tokens today`.

## Out of scope

- **Cumulative `tokens` field** (since-desktop-launch). Not displayed.
- **Token rate / per-minute trend.** Daily total only.
- **Persistent storage on the device.** All state comes from snapshots.
- **History card / sparkline.** Single number only.
- **Highlighting milestone crossings** (e.g. flash at 50K like the cdb
  reference's "celebrate" mode does). Static display.
- **Colour shifts based on usage volume.** White text always.
- **Display of `tokens_today` on the Eyes card or anywhere else.**
  StatusCard only.
