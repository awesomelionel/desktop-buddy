# Permission Buttons Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the user respond to Claude Desktop permission prompts on the device by selecting `Approve` / `Deny` / `Dismiss` with the three onboard buttons (D2 up, D0 down, D1 center) and writing the chosen decision back over the existing BLE Nordic UART Service.

**Architecture:** Add two new pure-logic libraries (`lib/buttons` for debouncing, `lib/prompt_ui` for the selection state machine), extend `lib/protocol` to parse the snapshot's `prompt` object into a new `ClaudePrompt` struct, add a `ble_write_line()` writer to `src/ble_bridge`, and wire it all together in `src/main.cpp` with a new `render_prompt(view)` branch. Pure-logic libraries are unit-tested under `pio test -e native` matching the existing `lib/protocol` and `lib/state` pattern.

**Tech Stack:** PlatformIO, Arduino framework on ESP32-S3, Adafruit GFX + ST7789 for the TFT, ArduinoJson 7 for JSON parsing, Unity for host tests, BLEDevice (the bundled ESP32 BLE stack) for transport. Reference design at `docs/superpowers/specs/2026-04-27-permission-buttons-design.md`.

---

## Background reading

Read these once before starting; don't try to memorize them — refer back as needed:

- `docs/superpowers/specs/2026-04-27-permission-buttons-design.md` — the full design.
- `cdb/REFERENCE.md` — wire protocol. Key sections: "Heartbeat snapshot" (the `prompt` field), "Permission decisions" (response shape), "Commands and acks" (TX direction).
- `cdb/src/hal/buttons.cpp` — reference impl's pin polarity for this exact board (D0 LOW pull-up, D1/D2 HIGH pull-down).
- `lib/protocol/protocol.cpp` — partial-update parser pattern this plan extends.
- `test/test_protocol/test_protocol.cpp` — Unity test style we'll match.
- `src/ble_bridge.cpp` — existing BLE peripheral with the TX characteristic already declared but unused.

## Conventions

- Match the existing code style: 4-space indent (no tabs), `static` for file-local helpers, comments only where they explain **why** (see `src/main.cpp` for examples).
- No emojis in code or commits.
- Commit after every passing task. Never `--no-verify`.
- If a step's test would conflict with an unmerged earlier step, run earlier steps first — tasks are sequenced, not independent.
- All `pio test -e native` runs are from the repo root: `/Users/lioneltan/code/claude-buddy`.
- All `pio run -t upload` runs require a flashed device; if you don't have one, do every step **except** the on-device tasks (Task 8) and report which ones you skipped.

---

## Task 1: Extend `ClaudeStatus` with `ClaudePrompt` skeleton

**Why this task:** Lay the type down first so later tasks can reference it. No parsing logic yet — just the struct, field, and confirmation that existing tests still pass.

**Files:**
- Modify: `lib/protocol/protocol.h`
- Test: `test/test_protocol/test_protocol.cpp` (no new tests in this task; just ensure existing 5 still pass)

- [ ] **Step 1.1: Add `ClaudePrompt` struct and field**

Replace the contents of `lib/protocol/protocol.h` with:

```c
#pragma once
#include <stdint.h>
#include <stddef.h>

struct ClaudePrompt {
    bool present;
    char id[24];
    char tool[16];
    char hint[64];
};

struct ClaudeStatus {
    uint8_t      total;
    uint8_t      running;
    uint8_t      waiting;
    char         msg[32];
    bool         valid;        // true once at least one snapshot has parsed
    ClaudePrompt prompt;
};

// Parse one newline-stripped JSON object from the bridge into `out`.
// Missing fields are left at whatever they already were in `out` so that
// successive partial snapshots accumulate. Returns false if the line isn't
// a JSON object, json fails to parse, or `line`/`out` is null.
bool protocol_parse_line(const char* line, ClaudeStatus* out);
```

- [ ] **Step 1.2: Run existing protocol tests to confirm they still pass**

Run:
```sh
pio test -e native -f test_protocol
```
Expected: 5 tests pass (the existing ones — `test_parse_full_snapshot`, `test_parse_missing_fields_keeps_previous`, `test_parse_rejects_non_object`, `test_parse_rejects_malformed_json`, `test_parse_truncates_long_msg`).

If a test fails, the struct change probably broke a zero-init assumption — verify `ClaudeStatus s = {}` still zero-initializes the new `prompt` field (it should, value-initialization recurses).

- [ ] **Step 1.3: Run state tests to confirm they still pass**

Run:
```sh
pio test -e native -f test_state
```
Expected: 6 tests pass (state derivation tests use `ClaudeStatus` too — make sure the layout change didn't break them).

- [ ] **Step 1.4: Commit**

```sh
git add lib/protocol/protocol.h
git commit -m "feat(protocol): add ClaudePrompt struct (no parsing yet)"
```

---

## Task 2: Parse the `prompt` object from snapshots

**Why this task:** Extends the parser to populate `ClaudeStatus.prompt`, with all the edge cases specified in the design (snapshot detection, missing id, truncation, hint sanitization).

**Files:**
- Modify: `lib/protocol/protocol.cpp`
- Test: `test/test_protocol/test_protocol.cpp`

- [ ] **Step 2.1: Add the failing tests**

Open `test/test_protocol/test_protocol.cpp`. After `test_parse_truncates_long_msg` and before `int main(...)`, add these tests:

```c
static void test_parse_prompt_full(void) {
    ClaudeStatus s = {};
    bool ok = protocol_parse_line(
        "{\"total\":1,\"running\":0,\"waiting\":1,\"prompt\":"
        "{\"id\":\"req_abc123\",\"tool\":\"Bash\",\"hint\":\"rm -rf /tmp/foo\"}}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(s.prompt.present);
    TEST_ASSERT_EQUAL_STRING("req_abc123", s.prompt.id);
    TEST_ASSERT_EQUAL_STRING("Bash", s.prompt.tool);
    TEST_ASSERT_EQUAL_STRING("rm -rf /tmp/foo", s.prompt.hint);
}

static void test_parse_snapshot_without_prompt_clears(void) {
    ClaudeStatus s = {};
    s.prompt.present = true;
    strcpy(s.prompt.id, "req_old");
    bool ok = protocol_parse_line("{\"total\":1,\"running\":1,\"waiting\":0}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(s.prompt.present);
}

static void test_parse_non_snapshot_does_not_clear_prompt(void) {
    ClaudeStatus s = {};
    s.prompt.present = true;
    strcpy(s.prompt.id, "req_keep");
    // An ack-shaped JSON (no `total` field) should leave prompt alone.
    bool ok = protocol_parse_line("{\"ack\":\"name\",\"ok\":true}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(s.prompt.present);
    TEST_ASSERT_EQUAL_STRING("req_keep", s.prompt.id);
}

static void test_parse_prompt_missing_id_drops(void) {
    ClaudeStatus s = {};
    bool ok = protocol_parse_line(
        "{\"total\":1,\"prompt\":{\"tool\":\"Bash\",\"hint\":\"x\"}}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(s.prompt.present);
}

static void test_parse_prompt_id_too_long_drops(void) {
    // sizeof(id) == 24, so 23 chars is the longest that fits with NUL.
    // Send 30 chars → must NOT silently truncate.
    ClaudeStatus s = {};
    bool ok = protocol_parse_line(
        "{\"total\":1,\"prompt\":{\"id\":\"abcdefghijklmnopqrstuvwxyz1234\","
        "\"tool\":\"Bash\",\"hint\":\"x\"}}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(s.prompt.present);
}

static void test_parse_prompt_hint_sanitizes_unprintable(void) {
    ClaudeStatus s = {};
    bool ok = protocol_parse_line(
        "{\"total\":1,\"prompt\":{\"id\":\"r1\",\"tool\":\"Bash\","
        "\"hint\":\"a\\nb\\tc\"}}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(s.prompt.present);
    TEST_ASSERT_EQUAL_STRING("a?b?c", s.prompt.hint);
}
```

Then add their `RUN_TEST` lines inside `main`, after the existing `RUN_TEST(test_parse_truncates_long_msg);`:

```c
    RUN_TEST(test_parse_prompt_full);
    RUN_TEST(test_parse_snapshot_without_prompt_clears);
    RUN_TEST(test_parse_non_snapshot_does_not_clear_prompt);
    RUN_TEST(test_parse_prompt_missing_id_drops);
    RUN_TEST(test_parse_prompt_id_too_long_drops);
    RUN_TEST(test_parse_prompt_hint_sanitizes_unprintable);
```

- [ ] **Step 2.2: Run tests, confirm they fail**

Run:
```sh
pio test -e native -f test_protocol
```
Expected: 5 existing pass, 6 new fail (all should fail with assertion errors, not crashes — the struct exists so compilation works).

- [ ] **Step 2.3: Implement the parser changes**

Replace the contents of `lib/protocol/protocol.cpp` with:

```c
#include "protocol.h"
#include <ArduinoJson.h>
#include <string.h>

// Sanitize bytes from `src` into `dst` (size n incl. NUL): replace any
// non-printable byte (including '\n', '\t', '\r') with '?', stop at NUL or
// when the buffer is full. Always NUL-terminates.
static void sanitize_copy(char* dst, size_t n, const char* src) {
    if (n == 0) return;
    size_t i = 0;
    if (src) {
        for (; i < n - 1 && src[i]; i++) {
            unsigned char c = (unsigned char)src[i];
            dst[i] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
        }
    }
    dst[i] = 0;
}

bool protocol_parse_line(const char* line, ClaudeStatus* out) {
    if (!line || !out) return false;
    if (line[0] != '{') return false;

    JsonDocument doc;
    if (deserializeJson(doc, line)) return false;
    if (!doc.is<JsonObject>()) return false;

    // We treat any object containing `total` as a snapshot. Other line
    // shapes (acks, time-sync, owner) leave existing prompt state alone
    // so an ack reply can't accidentally clear an active prompt.
    bool isSnapshot = doc["total"].is<unsigned int>();

    if (isSnapshot) {
        out->total   = doc["total"]   | out->total;
        out->running = doc["running"] | out->running;
        out->waiting = doc["waiting"] | out->waiting;

        const char* m = doc["msg"];
        if (m) {
            strncpy(out->msg, m, sizeof(out->msg) - 1);
            out->msg[sizeof(out->msg) - 1] = 0;
        }

        // Default the prompt to "absent" for this snapshot, then
        // overwrite if a valid one is present.
        out->prompt.present = false;

        JsonObject p = doc["prompt"];
        if (!p.isNull()) {
            const char* id   = p["id"];
            const char* tool = p["tool"];
            const char* hint = p["hint"];
            // Echoing back a truncated id would silently no-op on the
            // desktop, so refuse a prompt we can't faithfully respond to.
            if (id && strlen(id) < sizeof(out->prompt.id)) {
                strncpy(out->prompt.id, id, sizeof(out->prompt.id) - 1);
                out->prompt.id[sizeof(out->prompt.id) - 1] = 0;
                sanitize_copy(out->prompt.tool, sizeof(out->prompt.tool), tool);
                sanitize_copy(out->prompt.hint, sizeof(out->prompt.hint), hint);
                out->prompt.present = true;
            }
        }

        out->valid = true;
    }

    return true;
}
```

- [ ] **Step 2.4: Run tests, confirm they pass**

Run:
```sh
pio test -e native -f test_protocol
```
Expected: 11 tests pass (5 existing + 6 new).

If `test_parse_non_snapshot_does_not_clear_prompt` returns `ok=false`, that's because the existing parser unconditionally returned true for any JSON object. The new code returns `true` for any JSON object even when it's a non-snapshot — verify the early-return path is not dropping non-snapshots.

- [ ] **Step 2.5: Commit**

```sh
git add lib/protocol/protocol.cpp test/test_protocol/test_protocol.cpp
git commit -m "feat(protocol): parse prompt object from snapshots"
```

---

## Task 3: New `lib/buttons` debouncer

**Why this task:** A self-contained, host-testable debouncer that the main loop will feed raw GPIO booleans to. Polarity is the caller's problem.

**Files:**
- Create: `lib/buttons/buttons.h`
- Create: `lib/buttons/buttons.cpp`
- Create: `test/test_buttons/test_buttons.cpp`

- [ ] **Step 3.1: Create the header**

Write `lib/buttons/buttons.h`:

```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

// Polarity-agnostic debouncer for three buttons. The caller reads the
// hardware pins (with whatever pull and polarity it needs) and passes
// raw booleans where `true` means "pressed". Events fire on the
// transition from not-pressed to pressed of the *debounced* state.

enum ButtonEvent {
    BTN_NONE = 0,
    BTN_UP,
    BTN_DOWN,
    BTN_CENTER,
};

struct ButtonChan {
    bool     stable;        // current debounced state (true = pressed)
    bool     last_raw;      // last raw value seen
    uint32_t last_change;   // when last_raw last changed
    bool     initialized;   // false until the first step records baselines
};

struct Buttons {
    ButtonChan up;
    ButtonChan down;
    ButtonChan center;
};

void        buttons_init(struct Buttons* b);

// Advance the debouncer by one tick. `now_ms` should be a monotonic
// millisecond timestamp (millis()). Returns one event per call; if
// multiple channels would fire simultaneously, priority is
// CENTER > DOWN > UP and the others are absorbed (their pressed state
// is recorded but they emit nothing this call).
ButtonEvent buttons_step(struct Buttons* b, uint32_t now_ms,
                         bool up_raw, bool down_raw, bool center_raw);
```

- [ ] **Step 3.2: Create the test file with all tests written and failing**

Write `test/test_buttons/test_buttons.cpp`:

```c
#include <unity.h>
#include "buttons.h"

void setUp(void) {}
void tearDown(void) {}

static const uint32_t T_DEBOUNCE = 20;  // ms — must match buttons.cpp

static void test_no_event_on_high_pins(void) {
    Buttons b; buttons_init(&b);
    for (uint32_t t = 0; t < 100; t++) {
        TEST_ASSERT_EQUAL(BTN_NONE, buttons_step(&b, t, false, false, false));
    }
}

static void test_debounce_rejects_short_blip(void) {
    Buttons b; buttons_init(&b);
    // Up high for one tick at t=10, back low at t=15. Total low time 5 ms.
    buttons_step(&b, 0,  false, false, false);
    buttons_step(&b, 10, true,  false, false);
    buttons_step(&b, 15, false, false, false);
    // 50 ms later, no event has been emitted.
    for (uint32_t t = 16; t < 100; t++) {
        TEST_ASSERT_EQUAL(BTN_NONE, buttons_step(&b, t, false, false, false));
    }
}

static void test_emits_press_on_stable_low(void) {
    Buttons b; buttons_init(&b);
    buttons_step(&b, 0, false, false, false);
    buttons_step(&b, 1, true,  false, false);  // up goes pressed
    int events = 0;
    ButtonEvent last = BTN_NONE;
    for (uint32_t t = 2; t <= 1 + T_DEBOUNCE + 5; t++) {
        ButtonEvent ev = buttons_step(&b, t, true, false, false);
        if (ev != BTN_NONE) { events++; last = ev; }
    }
    TEST_ASSERT_EQUAL(1, events);
    TEST_ASSERT_EQUAL(BTN_UP, last);
}

static void test_no_repeat_while_held(void) {
    Buttons b; buttons_init(&b);
    buttons_step(&b, 0, false, false, false);
    int events = 0;
    for (uint32_t t = 1; t < 200; t++) {
        ButtonEvent ev = buttons_step(&b, t, true, false, false);
        if (ev != BTN_NONE) events++;
    }
    TEST_ASSERT_EQUAL(1, events);
}

static void test_re_arms_after_release(void) {
    Buttons b; buttons_init(&b);
    buttons_step(&b, 0, false, false, false);
    int events = 0;
    // Press, release, press — each held longer than the debounce window.
    for (uint32_t t = 1;   t < 1 + T_DEBOUNCE + 5;  t++)
        if (buttons_step(&b, t, true,  false, false) == BTN_UP) events++;
    for (uint32_t t = 50;  t < 50 + T_DEBOUNCE + 5; t++)
        buttons_step(&b, t, false, false, false);
    for (uint32_t t = 100; t < 100 + T_DEBOUNCE + 5; t++)
        if (buttons_step(&b, t, true,  false, false) == BTN_UP) events++;
    TEST_ASSERT_EQUAL(2, events);
}

static void test_priority_when_simultaneous(void) {
    Buttons b; buttons_init(&b);
    buttons_step(&b, 0, false, false, false);
    int up_events = 0, down_events = 0, center_events = 0;
    for (uint32_t t = 1; t < 1 + T_DEBOUNCE + 5; t++) {
        ButtonEvent ev = buttons_step(&b, t, true, true, true);
        if (ev == BTN_UP)     up_events++;
        if (ev == BTN_DOWN)   down_events++;
        if (ev == BTN_CENTER) center_events++;
    }
    TEST_ASSERT_EQUAL(1, center_events);
    TEST_ASSERT_EQUAL(0, down_events);
    TEST_ASSERT_EQUAL(0, up_events);
}

static void test_held_at_boot_does_not_fire(void) {
    Buttons b; buttons_init(&b);
    // First step sees up already pressed — debouncer treats that as
    // baseline, no falling edge has been observed.
    int events = 0;
    for (uint32_t t = 0; t < 200; t++) {
        ButtonEvent ev = buttons_step(&b, t, true, false, false);
        if (ev != BTN_NONE) events++;
    }
    TEST_ASSERT_EQUAL(0, events);
    // After release and re-press, we get exactly one event.
    for (uint32_t t = 200; t < 200 + T_DEBOUNCE + 5; t++)
        buttons_step(&b, t, false, false, false);
    int events2 = 0;
    for (uint32_t t = 300; t < 300 + T_DEBOUNCE + 5; t++) {
        ButtonEvent ev = buttons_step(&b, t, true, false, false);
        if (ev != BTN_NONE) events2++;
    }
    TEST_ASSERT_EQUAL(1, events2);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_no_event_on_high_pins);
    RUN_TEST(test_debounce_rejects_short_blip);
    RUN_TEST(test_emits_press_on_stable_low);
    RUN_TEST(test_no_repeat_while_held);
    RUN_TEST(test_re_arms_after_release);
    RUN_TEST(test_priority_when_simultaneous);
    RUN_TEST(test_held_at_boot_does_not_fire);
    return UNITY_END();
}
```

- [ ] **Step 3.3: Run tests — must fail (link error, no implementation yet)**

Run:
```sh
pio test -e native -f test_buttons
```
Expected: build/link failure — `buttons_init` and `buttons_step` are undefined. That's intentional; we haven't written `buttons.cpp` yet.

- [ ] **Step 3.4: Implement the debouncer**

Write `lib/buttons/buttons.cpp`:

```c
#include "buttons.h"

static const uint32_t T_DEBOUNCE = 20;

static void chan_init(ButtonChan* c) {
    c->stable      = false;
    c->last_raw    = false;
    c->last_change = 0;
    c->initialized = false;
}

// Advance one channel; return true if the debounced state transitioned
// from not-pressed to pressed during this step.
static bool chan_step(ButtonChan* c, uint32_t now, bool raw) {
    if (!c->initialized) {
        c->stable      = raw;
        c->last_raw    = raw;
        c->last_change = now;
        c->initialized = true;
        return false;  // never fire on the first sample
    }
    if (raw != c->last_raw) {
        c->last_raw    = raw;
        c->last_change = now;
        return false;
    }
    if (raw != c->stable && (now - c->last_change) >= T_DEBOUNCE) {
        bool was = c->stable;
        c->stable = raw;
        return raw && !was;
    }
    return false;
}

void buttons_init(Buttons* b) {
    chan_init(&b->up);
    chan_init(&b->down);
    chan_init(&b->center);
}

ButtonEvent buttons_step(Buttons* b, uint32_t now_ms,
                         bool up_raw, bool down_raw, bool center_raw) {
    bool up_fire     = chan_step(&b->up,     now_ms, up_raw);
    bool down_fire   = chan_step(&b->down,   now_ms, down_raw);
    bool center_fire = chan_step(&b->center, now_ms, center_raw);

    // Priority: CENTER > DOWN > UP. Lower-priority fires are absorbed
    // (their state already advanced inside chan_step, just discard).
    if (center_fire) return BTN_CENTER;
    if (down_fire)   return BTN_DOWN;
    if (up_fire)     return BTN_UP;
    return BTN_NONE;
}
```

- [ ] **Step 3.5: Run tests, confirm they pass**

Run:
```sh
pio test -e native -f test_buttons
```
Expected: 7 tests pass.

If `test_priority_when_simultaneous` fails with `down_events=1` or `up_events=1`, your priority guard is firing more than one event per call — make sure only one of the three `return` statements runs.

- [ ] **Step 3.6: Run the full native suite to confirm nothing regressed**

Run:
```sh
pio test -e native
```
Expected: 18 tests pass (11 protocol + 7 buttons + 6 state — plus any state tests).

- [ ] **Step 3.7: Commit**

```sh
git add lib/buttons/ test/test_buttons/
git commit -m "feat(buttons): add debouncer library with host tests"
```

---

## Task 4: New `lib/prompt_ui` selection state machine

**Why this task:** Owns the visible-vs-hidden, highlight, dismissal-memory, flash-timer logic that the spec calls out — entirely host-testable.

**Files:**
- Create: `lib/prompt_ui/prompt_ui.h`
- Create: `lib/prompt_ui/prompt_ui.cpp`
- Create: `test/test_prompt_ui/test_prompt_ui.cpp`

- [ ] **Step 4.1: Create the header**

Write `lib/prompt_ui/prompt_ui.h`:

```c
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "protocol.h"   // ClaudePrompt
#include "buttons.h"    // ButtonEvent

enum PromptOption : uint8_t {
    OPT_APPROVE = 0,
    OPT_DENY    = 1,
    OPT_DISMISS = 2,
};

struct PromptView {
    bool         visible;
    const char*  tool;
    const char*  hint;
    PromptOption highlight;
    const char*  flash_text;   // null when not flashing
    uint16_t     flash_color;  // RGB565; meaningful only when flash_text != null
};

struct PromptUi {
    bool         visible;
    char         current_id[24];
    char         tool[16];
    char         hint[64];
    PromptOption highlight;
    char         last_dismissed_id[24];

    bool         flashing;
    char         flash_text[16];
    uint16_t     flash_color;
    uint32_t     flash_deadline_ms;

    bool         pending_outgoing_set;
    char         pending_outgoing[96];
};

void prompt_ui_init  (PromptUi* ui);

// Reconcile UI state against an incoming snapshot. Hides the UI when
// `prompt.present` flips false or `live` is false. Reveals when a fresh,
// non-dismissed prompt arrives. Replaces if the visible id changes.
// Also fires the flash → hide transition once the deadline elapses.
void prompt_ui_update(PromptUi* ui, const ClaudePrompt& prompt,
                      bool live, uint32_t now_ms);

// Feed a debounced button event. Ignored when `!visible`.
void prompt_ui_button(PromptUi* ui, ButtonEvent ev, uint32_t now_ms);

// Read-only view used by main.cpp's renderer.
PromptView prompt_ui_view(const PromptUi* ui);

// Drain the queued outgoing JSON line, if any. Returns false if nothing
// to send. Caller copies into its own buffer; this clears the queue.
bool prompt_ui_take_outgoing(PromptUi* ui, char* buf, size_t buf_len);
```

- [ ] **Step 4.2: Create the test file with all tests written and failing**

Write `test/test_prompt_ui/test_prompt_ui.cpp`:

```c
#include <unity.h>
#include <string.h>
#include "prompt_ui.h"

void setUp(void) {}
void tearDown(void) {}

static ClaudePrompt make_prompt(const char* id, const char* tool, const char* hint) {
    ClaudePrompt p = {};
    p.present = true;
    strncpy(p.id, id, sizeof(p.id) - 1);
    strncpy(p.tool, tool, sizeof(p.tool) - 1);
    strncpy(p.hint, hint, sizeof(p.hint) - 1);
    return p;
}

static ClaudePrompt make_absent(void) {
    ClaudePrompt p = {};
    p.present = false;
    return p;
}

static void test_hidden_until_prompt_present(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_absent(), true, 0);
    TEST_ASSERT_FALSE(prompt_ui_view(&ui).visible);
}

static void test_shows_with_default_approve(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r1", "Bash", "ls"), true, 100);
    PromptView v = prompt_ui_view(&ui);
    TEST_ASSERT_TRUE(v.visible);
    TEST_ASSERT_EQUAL(OPT_APPROVE, v.highlight);
    TEST_ASSERT_EQUAL_STRING("Bash", v.tool);
    TEST_ASSERT_EQUAL_STRING("ls", v.hint);
}

static void test_up_down_navigation_clamped(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_DOWN, 1);
    TEST_ASSERT_EQUAL(OPT_DENY, prompt_ui_view(&ui).highlight);
    prompt_ui_button(&ui, BTN_DOWN, 2);
    TEST_ASSERT_EQUAL(OPT_DISMISS, prompt_ui_view(&ui).highlight);
    prompt_ui_button(&ui, BTN_DOWN, 3);  // clamp
    TEST_ASSERT_EQUAL(OPT_DISMISS, prompt_ui_view(&ui).highlight);
    prompt_ui_button(&ui, BTN_UP,   4);
    prompt_ui_button(&ui, BTN_UP,   5);
    prompt_ui_button(&ui, BTN_UP,   6);  // clamp
    TEST_ASSERT_EQUAL(OPT_APPROVE, prompt_ui_view(&ui).highlight);
}

static void test_center_on_approve_emits_once_json(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r_xyz", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_CENTER, 1);
    char buf[128] = {};
    TEST_ASSERT_TRUE(prompt_ui_take_outgoing(&ui, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING(
        "{\"cmd\":\"permission\",\"id\":\"r_xyz\",\"decision\":\"once\"}", buf);
    TEST_ASSERT_FALSE(prompt_ui_take_outgoing(&ui, buf, sizeof(buf)));
}

static void test_center_on_deny_emits_deny_json(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r_xyz", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_DOWN, 1);   // → DENY
    prompt_ui_button(&ui, BTN_CENTER, 2);
    char buf[128] = {};
    TEST_ASSERT_TRUE(prompt_ui_take_outgoing(&ui, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING(
        "{\"cmd\":\"permission\",\"id\":\"r_xyz\",\"decision\":\"deny\"}", buf);
}

static void test_center_on_dismiss_emits_nothing(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_DOWN, 1);
    prompt_ui_button(&ui, BTN_DOWN, 2);   // → DISMISS
    prompt_ui_button(&ui, BTN_CENTER, 3);
    char buf[128] = {};
    TEST_ASSERT_FALSE(prompt_ui_take_outgoing(&ui, buf, sizeof(buf)));
}

static void test_dismiss_is_sticky_per_id(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_DOWN, 1);
    prompt_ui_button(&ui, BTN_DOWN, 2);
    prompt_ui_button(&ui, BTN_CENTER, 3);  // dismiss rA
    // After flash window, UI hides.
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 600);
    TEST_ASSERT_FALSE(prompt_ui_view(&ui).visible);
    // New id rB shows again.
    prompt_ui_update(&ui, make_prompt("rB", "Bash", ""), true, 700);
    TEST_ASSERT_TRUE(prompt_ui_view(&ui).visible);
}

static void test_auto_hide_when_prompt_disappears(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), true, 0);
    TEST_ASSERT_TRUE(prompt_ui_view(&ui).visible);
    prompt_ui_update(&ui, make_absent(), true, 1);
    TEST_ASSERT_FALSE(prompt_ui_view(&ui).visible);
    char buf[128] = {};
    TEST_ASSERT_FALSE(prompt_ui_take_outgoing(&ui, buf, sizeof(buf)));
}

static void test_auto_hide_when_offline(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), true, 0);
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), false, 1);  // OFFLINE
    TEST_ASSERT_FALSE(prompt_ui_view(&ui).visible);
}

static void test_new_id_replaces_visible_prompt(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("rA", "Bash", "x"), true, 0);
    prompt_ui_button(&ui, BTN_DOWN, 1);  // highlight = DENY
    TEST_ASSERT_EQUAL(OPT_DENY, prompt_ui_view(&ui).highlight);
    prompt_ui_update(&ui, make_prompt("rB", "Read", "y"), true, 2);
    PromptView v = prompt_ui_view(&ui);
    TEST_ASSERT_TRUE(v.visible);
    TEST_ASSERT_EQUAL(OPT_APPROVE, v.highlight);
    TEST_ASSERT_EQUAL_STRING("Read", v.tool);
}

static void test_flash_clears_after_500ms(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_CENTER, 1);  // approve at t=1
    // At t=499 still visible + flashing.
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), true, 499);
    PromptView v1 = prompt_ui_view(&ui);
    TEST_ASSERT_TRUE(v1.visible);
    TEST_ASSERT_NOT_NULL(v1.flash_text);
    // At t=501 hidden.
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), true, 501);
    TEST_ASSERT_FALSE(prompt_ui_view(&ui).visible);
}

static void test_press_while_hidden_is_noop(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_button(&ui, BTN_CENTER, 1);  // no UI to act on
    char buf[128] = {};
    TEST_ASSERT_FALSE(prompt_ui_take_outgoing(&ui, buf, sizeof(buf)));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_hidden_until_prompt_present);
    RUN_TEST(test_shows_with_default_approve);
    RUN_TEST(test_up_down_navigation_clamped);
    RUN_TEST(test_center_on_approve_emits_once_json);
    RUN_TEST(test_center_on_deny_emits_deny_json);
    RUN_TEST(test_center_on_dismiss_emits_nothing);
    RUN_TEST(test_dismiss_is_sticky_per_id);
    RUN_TEST(test_auto_hide_when_prompt_disappears);
    RUN_TEST(test_auto_hide_when_offline);
    RUN_TEST(test_new_id_replaces_visible_prompt);
    RUN_TEST(test_flash_clears_after_500ms);
    RUN_TEST(test_press_while_hidden_is_noop);
    return UNITY_END();
}
```

- [ ] **Step 4.3: Run — must fail (no implementation)**

Run:
```sh
pio test -e native -f test_prompt_ui
```
Expected: link failure — `prompt_ui_init`, `prompt_ui_update`, `prompt_ui_button`, `prompt_ui_view`, `prompt_ui_take_outgoing` are undefined.

- [ ] **Step 4.4: Implement the state machine**

Write `lib/prompt_ui/prompt_ui.cpp`:

```c
#include "prompt_ui.h"
#include <string.h>
#include <stdio.h>

// RGB565 — same encoding the rest of the firmware uses (Adafruit GFX
// ST77XX colors). Defined here as plain constants so the library
// compiles host-side without pulling Adafruit_GFX in.
static const uint16_t COLOR_GREEN  = 0x07E0;
static const uint16_t COLOR_RED    = 0xF800;
static const uint16_t COLOR_YELLOW = 0xFFE0;

static const uint32_t FLASH_MS = 500;

static void hide(PromptUi* ui) {
    ui->visible          = false;
    ui->flashing         = false;
    ui->flash_text[0]    = 0;
    ui->flash_deadline_ms = 0;
    // Note: do NOT clear pending_outgoing here. A pending response
    // queued just before hiding (e.g., from CENTER) must still drain.
}

static void show(PromptUi* ui, const ClaudePrompt& p) {
    ui->visible = true;
    strncpy(ui->current_id, p.id,   sizeof(ui->current_id) - 1);
    ui->current_id[sizeof(ui->current_id) - 1] = 0;
    strncpy(ui->tool,       p.tool, sizeof(ui->tool) - 1);
    ui->tool[sizeof(ui->tool) - 1] = 0;
    strncpy(ui->hint,       p.hint, sizeof(ui->hint) - 1);
    ui->hint[sizeof(ui->hint) - 1] = 0;
    ui->highlight = OPT_APPROVE;
    ui->flashing  = false;
    ui->flash_text[0] = 0;
    ui->flash_deadline_ms = 0;
}

void prompt_ui_init(PromptUi* ui) {
    memset(ui, 0, sizeof(*ui));
}

void prompt_ui_update(PromptUi* ui, const ClaudePrompt& p,
                      bool live, uint32_t now_ms) {
    // Fire flash → hide first so the rest of the function operates on
    // post-flash state.
    if (ui->visible && ui->flashing && now_ms >= ui->flash_deadline_ms) {
        hide(ui);
    }

    if (!live) {
        if (ui->visible) hide(ui);
        return;
    }

    if (!p.present) {
        if (ui->visible) hide(ui);
        return;
    }

    // p.present && live
    if (strcmp(p.id, ui->last_dismissed_id) == 0) {
        // Same id was previously dismissed. Stay (or become) hidden.
        if (ui->visible) hide(ui);
        return;
    }

    if (ui->visible && strcmp(p.id, ui->current_id) == 0) {
        return;  // same prompt, no change
    }

    show(ui, p);
}

static void queue_outgoing(PromptUi* ui, const char* decision) {
    snprintf(ui->pending_outgoing, sizeof(ui->pending_outgoing),
             "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"%s\"}",
             ui->current_id, decision);
    ui->pending_outgoing_set = true;
}

static void start_flash(PromptUi* ui, const char* text, uint16_t color,
                        uint32_t now_ms) {
    ui->flashing = true;
    strncpy(ui->flash_text, text, sizeof(ui->flash_text) - 1);
    ui->flash_text[sizeof(ui->flash_text) - 1] = 0;
    ui->flash_color = color;
    ui->flash_deadline_ms = now_ms + FLASH_MS;
}

void prompt_ui_button(PromptUi* ui, ButtonEvent ev, uint32_t now_ms) {
    if (!ui->visible || ui->flashing) return;

    switch (ev) {
        case BTN_UP:
            if (ui->highlight > 0)
                ui->highlight = (PromptOption)(ui->highlight - 1);
            return;
        case BTN_DOWN:
            if (ui->highlight < OPT_DISMISS)
                ui->highlight = (PromptOption)(ui->highlight + 1);
            return;
        case BTN_CENTER:
            switch (ui->highlight) {
                case OPT_APPROVE:
                    queue_outgoing(ui, "once");
                    start_flash(ui, "SENT: APPROVE", COLOR_GREEN, now_ms);
                    return;
                case OPT_DENY:
                    queue_outgoing(ui, "deny");
                    start_flash(ui, "SENT: DENY", COLOR_RED, now_ms);
                    return;
                case OPT_DISMISS:
                    strncpy(ui->last_dismissed_id, ui->current_id,
                            sizeof(ui->last_dismissed_id) - 1);
                    ui->last_dismissed_id[sizeof(ui->last_dismissed_id) - 1] = 0;
                    start_flash(ui, "DISMISSED", COLOR_YELLOW, now_ms);
                    return;
            }
            return;
        case BTN_NONE:
        default:
            return;
    }
}

PromptView prompt_ui_view(const PromptUi* ui) {
    PromptView v = {};
    v.visible     = ui->visible;
    v.tool        = ui->tool;
    v.hint        = ui->hint;
    v.highlight   = ui->highlight;
    v.flash_text  = ui->flashing ? ui->flash_text : nullptr;
    v.flash_color = ui->flash_color;
    return v;
}

bool prompt_ui_take_outgoing(PromptUi* ui, char* buf, size_t buf_len) {
    if (!ui->pending_outgoing_set) return false;
    strncpy(buf, ui->pending_outgoing, buf_len - 1);
    buf[buf_len - 1] = 0;
    ui->pending_outgoing_set = false;
    ui->pending_outgoing[0]  = 0;
    return true;
}
```

- [ ] **Step 4.5: Run tests, confirm they pass**

Run:
```sh
pio test -e native -f test_prompt_ui
```
Expected: 12 tests pass.

If `test_dismiss_is_sticky_per_id` fails because the same id reappears as visible, check that `last_dismissed_id` is initialized to `""` by `prompt_ui_init` (the `memset` covers it) and that the dismiss path is writing into it before the next `update`.

If `test_flash_clears_after_500ms` fails — the flash-expiry guard runs at the **top** of `prompt_ui_update`, then `live`/`p.present` checks follow. The order matters because expiry must happen even on the "happy path" snapshots that don't change anything else.

- [ ] **Step 4.6: Run the full native suite**

Run:
```sh
pio test -e native
```
Expected: 30 tests pass (11 protocol + 7 buttons + 12 prompt_ui + ~6 state).

- [ ] **Step 4.7: Commit**

```sh
git add lib/prompt_ui/ test/test_prompt_ui/
git commit -m "feat(prompt_ui): add selection state machine with host tests"
```

---

## Task 5: Add `ble_write_line` to the BLE bridge

**Why this task:** The existing `src/ble_bridge.cpp` declares the TX characteristic but never sends. We need a way to push a JSON line out over notify.

**Files:**
- Modify: `src/ble_bridge.h`
- Modify: `src/ble_bridge.cpp`

No host test — this is hardware-coupled. We'll verify on device in Task 8.

- [ ] **Step 5.1: Update the header**

Replace the contents of `src/ble_bridge.h` with:

```c
#pragma once
#include <stddef.h>
#include <stdint.h>

// Nordic UART Service-compatible BLE peripheral. Claude Desktop
// (and any nRF/Web Bluetooth client) writes JSON lines into the RX
// characteristic; we drop the bytes into a ring buffer that the main
// loop drains via ble_available()/ble_read(), accumulating until it
// sees '\n' and handing the line to protocol_parse_line.
//
// Service: 6e400001-b5a3-f393-e0a9-e50e24dcca9e
// RX char: 6e400002-b5a3-f393-e0a9-e50e24dcca9e (write)
// TX char: 6e400003-b5a3-f393-e0a9-e50e24dcca9e (notify)

void   ble_init(const char* device_name);
bool   ble_connected();
size_t ble_available();
int    ble_read();   // -1 if empty

// Push `line` followed by '\n' out the TX characteristic via notify.
// Returns false if not connected. Best-effort: if the notify queue is
// full, returns false; the caller should drop, not retry.
bool   ble_write_line(const char* line);
```

- [ ] **Step 5.2: Implement the writer**

Open `src/ble_bridge.cpp`. Append after the existing `int ble_read()` definition (which is the last function):

```c
bool ble_write_line(const char* line) {
    if (!connected || !txChar || !line) return false;

    size_t n = strlen(line);
    // Out-of-line guard: keep payloads small. NUS over notify with
    // setMTU(517) gives ~514 bytes per packet — our outgoing lines
    // are well under 100 bytes.
    if (n > 256) return false;

    // Build "<line>\n" in a small stack buffer and notify in one shot.
    char buf[260];
    memcpy(buf, line, n);
    buf[n]     = '\n';
    buf[n + 1] = 0;

    txChar->setValue((uint8_t*)buf, n + 1);
    txChar->notify();
    return true;
}
```

You'll also need `#include <string.h>` if it isn't already; check the top of `src/ble_bridge.cpp` — `<Arduino.h>` already pulls it in transitively, so this is usually unnecessary, but add it explicitly if the build complains.

- [ ] **Step 5.3: Compile to confirm no regressions**

Run:
```sh
pio run -e adafruit_feather_esp32s3_reversetft
```
Expected: build succeeds. If there's a linker error about `txChar` having internal linkage and being referenced from outside its translation unit — it's fine, `txChar` is a file-static and `ble_write_line` lives in the same `.cpp`.

- [ ] **Step 5.4: Commit**

```sh
git add src/ble_bridge.h src/ble_bridge.cpp
git commit -m "feat(ble): add ble_write_line via TX notify"
```

---

## Task 6: Wire main loop — pin setup, button polling, prompt UI updates

**Why this task:** Connect the libraries to the actual hardware loop. No new logic; just glue.

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 6.1: Update includes and global state**

In `src/main.cpp`, locate the existing top-of-file includes:

```c
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <esp_mac.h>

#include "ble_bridge.h"
#include "protocol.h"
#include "state.h"
```

Replace that block with:

```c
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <esp_mac.h>

#include "ble_bridge.h"
#include "buttons.h"
#include "prompt_ui.h"
#include "protocol.h"
#include "state.h"
```

Then locate the existing static globals near the top:

```c
static char         deviceName[16] = "Claude";
static ClaudeStatus status         = {};
static BuddyState   currentState   = STATE_DISCONNECTED;
static BuddyState   lastDrawnState = (BuddyState)0xFF;
static char         lastDrawnMsg[sizeof(status.msg)] = {};
static uint32_t     lastSnapshotMs = 0;
```

Add immediately after them:

```c
static Buttons      btns           = {};
static PromptUi     promptUi       = {};
// Track previously rendered prompt UI shape so we redraw only on change.
static bool         lastPromptVisible    = false;
static PromptOption lastPromptHighlight  = OPT_APPROVE;
static char         lastPromptId[24]     = {};
static bool         lastPromptFlashing   = false;
```

- [ ] **Step 6.2: Pin setup in `setup()`**

Locate the existing `setup()` — specifically the line `ble_init(deviceName);`. Insert immediately **before** it:

```c
    pinMode(0, INPUT_PULLUP);    // D0 / BOOT — active LOW (Down)
    pinMode(1, INPUT_PULLDOWN);  // D1 — active HIGH (Center)
    pinMode(2, INPUT_PULLDOWN);  // D2 — active HIGH (Up)
    buttons_init(&btns);
    prompt_ui_init(&promptUi);
```

- [ ] **Step 6.3: Add the `render_prompt` function**

In `src/main.cpp`, immediately **before** the existing `static void render() {` definition, add:

```c
static void render_prompt(const PromptView& v) {
    tft.fillScreen(ST77XX_BLACK);

    // Header: "PERMISSION?" centered, size 2.
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    int16_t x1, y1; uint16_t tw, th;
    const char* hdr = "PERMISSION?";
    tft.getTextBounds(hdr, 0, 0, &x1, &y1, &tw, &th);
    tft.setCursor((W - (int)tw) / 2, 2);
    tft.print(hdr);

    // Tool/hint area — size 1.
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    tft.setCursor(8, 28);
    tft.printf("Tool: %.20s", v.tool ? v.tool : "");

    if (v.hint && v.hint[0]) {
        tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.setCursor(8, 40);
        tft.printf("%.38s", v.hint);
        if (strlen(v.hint) > 38) {
            tft.setCursor(8, 50);
            tft.printf("%.38s", v.hint + 38);
        }
    }

    // Options stack OR flash text.
    tft.setTextSize(2);
    if (v.flash_text) {
        // Center the flash text across the options block, at y=82
        // (the middle row's y).
        tft.setTextColor(v.flash_color, ST77XX_BLACK);
        tft.getTextBounds(v.flash_text, 0, 0, &x1, &y1, &tw, &th);
        tft.setCursor((W - (int)tw) / 2, 82);
        tft.print(v.flash_text);
    } else {
        const char* labels[3] = {"Approve", "Deny", "Dismiss"};
        const int   ys[3]     = {66, 82, 98};
        for (int i = 0; i < 3; i++) {
            bool hi = (i == (int)v.highlight);
            if (hi) {
                tft.fillRect(0, ys[i], W, 16, ST77XX_WHITE);
                tft.setTextColor(ST77XX_BLACK, ST77XX_WHITE);
                tft.setCursor(8, ys[i]);
                tft.print("> ");
                tft.print(labels[i]);
            } else {
                tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
                tft.setCursor(24, ys[i]);
                tft.print(labels[i]);
            }
        }
    }

    // Footer (same as the normal screen).
    tft.setTextSize(1);
    tft.setTextColor(isLive() ? ST77XX_GREEN : ST77XX_RED, ST77XX_BLACK);
    tft.setCursor(8, 118);
    tft.print(isLive() ? "LIVE  " : "OFFLN ");
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.print(deviceName);
}
```

Note: Adafruit GFX's default font does not include the unicode `▶` glyph, so we draw a plain `> ` prefix. The spec calls it `▶`; same intent, ASCII-safe.

- [ ] **Step 6.4: Update the loop body**

Locate the existing `void loop() {`. Replace its contents (everything between the opening `{` and the closing `}`) with:

```c
    // 1) Drain BLE RX bytes into a single line buffer.
    static char lineBuf[4097];
    static size_t lineLen = 0;
    static bool   lineOverflow = false;

    while (ble_available()) {
        int c = ble_read();
        if (c < 0) break;
        if (c == '\n' || c == '\r') {
            if (lineOverflow) {
                Serial.printf("[rx] line overflow (>%u bytes), dropped\n",
                              (unsigned)sizeof(lineBuf) - 1);
                lineOverflow = false;
            } else if (lineLen > 0) {
                lineBuf[lineLen] = 0;
                if (lineBuf[0] == '{') {
                    if (protocol_parse_line(lineBuf, &status)) {
                        lastSnapshotMs = millis();
                        Serial.printf("[rx] %s\n", lineBuf);
                    }
                }
            }
            lineLen = 0;
        } else if (lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = (char)c;
        } else {
            lineOverflow = true;
        }
    }

    uint32_t now = millis();

    // 2) Poll buttons, feed prompt_ui (only when visible — buttons inert otherwise).
    bool up_raw     = (digitalRead(2) == HIGH);
    bool down_raw   = (digitalRead(0) == LOW);
    bool center_raw = (digitalRead(1) == HIGH);
    ButtonEvent ev = buttons_step(&btns, now, up_raw, down_raw, center_raw);
    if (ev != BTN_NONE && prompt_ui_view(&promptUi).visible) {
        prompt_ui_button(&promptUi, ev, now);
    }

    // 3) Reconcile prompt UI against current snapshot/connection state.
    prompt_ui_update(&promptUi, status.prompt, isLive(), now);

    // 4) Drain any queued outgoing decision JSON.
    char outBuf[96];
    if (prompt_ui_take_outgoing(&promptUi, outBuf, sizeof(outBuf))) {
        if (!ble_write_line(outBuf)) {
            Serial.printf("[tx] dropped (not connected): %s\n", outBuf);
        } else {
            Serial.printf("[tx] %s\n", outBuf);
        }
    }

    // 5) Decide what to draw and whether to redraw.
    PromptView pv = prompt_ui_view(&promptUi);
    BuddyState next = state_derive(status, isLive());

    bool promptViewChanged =
        pv.visible != lastPromptVisible
        || pv.highlight != lastPromptHighlight
        || (pv.flash_text != nullptr) != lastPromptFlashing
        || (pv.visible && strcmp(lastPromptId, promptUi.current_id) != 0);
    bool stateChanged = (next != lastDrawnState);
    bool msgChanged   = strncmp(lastDrawnMsg, status.msg, sizeof(lastDrawnMsg)) != 0;

    if (pv.visible) {
        if (promptViewChanged) {
            render_prompt(pv);
            lastPromptVisible    = true;
            lastPromptHighlight  = pv.highlight;
            lastPromptFlashing   = (pv.flash_text != nullptr);
            strncpy(lastPromptId, promptUi.current_id, sizeof(lastPromptId) - 1);
            lastPromptId[sizeof(lastPromptId) - 1] = 0;
            // Force a normal-screen redraw next time we revert.
            lastDrawnState = (BuddyState)0xFF;
            lastDrawnMsg[0] = 0;
        }
    } else {
        if (lastPromptVisible || stateChanged || msgChanged) {
            currentState = next;
            render();
            lastDrawnState = next;
            strncpy(lastDrawnMsg, status.msg, sizeof(lastDrawnMsg) - 1);
            lastDrawnMsg[sizeof(lastDrawnMsg) - 1] = 0;
            lastPromptVisible = false;
        } else {
            // Even with no new data, flip to OFFLN once timeout elapses.
            static uint32_t lastTick = 0;
            if (now - lastTick > 1000) {
                lastTick = now;
                BuddyState recheck = state_derive(status, isLive());
                if (recheck != lastDrawnState) {
                    currentState = recheck;
                    render();
                    lastDrawnState = recheck;
                }
            }
        }
    }

    delay(20);
```

- [ ] **Step 6.5: Build the firmware**

Run:
```sh
pio run -e adafruit_feather_esp32s3_reversetft
```
Expected: build succeeds. Common issues:

- *"undefined reference to `prompt_ui_init`"* — `lib_ldf_mode = deep` in `platformio.ini` should pick up the new lib automatically; if it doesn't, run `pio run -t clean` first.
- *"`HIGH` undeclared in prompt_ui.cpp"* — that file is host-only; if the native build pulls in something Arduino-specific (it shouldn't), check that you didn't accidentally include `<Arduino.h>` anywhere in `lib/prompt_ui` or `lib/buttons`.
- *Type mismatches between `protocol.h` and the existing parser* — `ClaudeStatus` is now bigger; ensure no fixed-size memcpy assumes the old size.

- [ ] **Step 6.6: Run the full native suite once more to confirm the wiring didn't regress libs**

Run:
```sh
pio test -e native
```
Expected: 30 tests pass.

- [ ] **Step 6.7: Commit**

```sh
git add src/main.cpp
git commit -m "feat(main): wire buttons + prompt UI into render loop"
```

---

## Task 7: Update PlatformIO native env so new tests are picked up

**Why this task:** PlatformIO usually auto-discovers `test/test_*` directories and `lib/*` libraries, but our new libs (`buttons`, `prompt_ui`) need to be visible to the native env's `lib_deps` chain. Verify this works as-is; if not, add explicit lib_deps.

**Files:**
- (Possibly) modify: `platformio.ini`

- [ ] **Step 7.1: Confirm tests are auto-discovered**

Run:
```sh
pio test -e native
```
Expected: All 30 tests run. If `test_buttons` or `test_prompt_ui` reports "no tests found" or fails to link, fall through to step 7.2; otherwise skip to step 7.3.

- [ ] **Step 7.2: (only if needed) Add explicit lib paths**

Edit `platformio.ini`. Locate the `[env:native]` block. If the auto-discovery failed in 7.1, ensure the env has `test_framework = unity` (already present) and `lib_ldf_mode = deep` — add:

```ini
[env:native]
platform = native
test_framework = unity
build_flags = -std=gnu++17 -Wall
lib_ldf_mode = deep
lib_deps =
    ${env.lib_deps}
```

Then re-run `pio test -e native`. Expected: tests now pass.

- [ ] **Step 7.3: Commit (only if `platformio.ini` actually changed)**

If you modified `platformio.ini`:
```sh
git add platformio.ini
git commit -m "chore(pio): enable deep lib resolution for native env"
```

If `platformio.ini` was untouched, skip this commit.

---

## Task 8: On-device manual verification

**Why this task:** Display rendering, BLE notify, GPIO debouncing on real hardware all need eyes-on validation. None of this is automatable from the host.

**Prereqs:** Adafruit Feather ESP32-S3 Reverse TFT flashed with the latest build, paired to Claude Desktop (Developer Mode → Hardware Buddy). A way to trigger a permission prompt on the desktop (run `claude` and ask for a tool that prompts, e.g. `Bash`).

- [ ] **Step 8.1: Flash and open the serial monitor**

Run:
```sh
pio run -e adafruit_feather_esp32s3_reversetft -t upload
pio device monitor
```
Expected: device boots, splash shows, serial prints `[ble] advertising as 'Claude-XXXX'`.

- [ ] **Step 8.2: Pair and observe steady-state**

Pair from Claude Desktop. Confirm:
- Display switches to the normal screen (`IDLE` or `WORKING`, footer shows `LIVE  Claude-XXXX`).
- Press D0/D1/D2 — nothing should happen, no flicker, serial shows nothing for buttons.

- [ ] **Step 8.3: Trigger a permission prompt**

In a Claude Desktop session, ask Claude to run a tool that requires permission (e.g. `please run "ls /tmp" with Bash`).

Expected on the device:
- Screen replaces with `PERMISSION?` header, `Tool: Bash`, the hint string, and three options with `Approve` highlighted (white bar).
- Footer still reads `LIVE  Claude-XXXX`.

- [ ] **Step 8.4: Navigate**

Press D2 (Up) — highlight stays on Approve (clamped).
Press D0 (Down) — highlight moves to Deny.
Press D0 (Down) — highlight moves to Dismiss.
Press D0 (Down) — highlight stays on Dismiss (clamped).
Press D2 (Up) twice — back to Approve.

- [ ] **Step 8.5: Approve path**

With Approve highlighted, press D1 (Center).

Expected:
- Screen shows `SENT: APPROVE` in green for ~500 ms.
- Serial: `[tx] {"cmd":"permission","id":"...","decision":"once"}`.
- Desktop allows the tool; the snapshot's next tick drops `prompt`; device reverts to the normal screen.

- [ ] **Step 8.6: Deny path**

Trigger a new prompt. Press D0, D1.

Expected:
- `SENT: DENY` flashes red.
- Serial: `[tx] {"cmd":"permission","id":"...","decision":"deny"}`.
- Desktop rejects the tool.

- [ ] **Step 8.7: Dismiss is sticky**

Trigger a prompt that the desktop will keep re-sending (i.e., the Claude turn is still blocked). Press D0, D0, D1 (Dismiss).

Expected:
- `DISMISSED` flashes yellow, no `[tx]` serial line.
- Device reverts to normal screen and **does not** show the prompt UI again, even though subsequent snapshots still carry the same `prompt.id`.
- Now resolve from the desktop dialog. Trigger another prompt — UI shows again because the new `prompt.id` differs.

- [ ] **Step 8.8: Disconnect during prompt**

Trigger a prompt. While the prompt UI is up, force a disconnect (turn off Bluetooth on the desktop, or close Claude Desktop).

Expected:
- Within ~30 s, footer flips to red `OFFLN` and the prompt UI clears immediately back to the normal screen.
- Reconnect. Trigger another prompt — UI reappears for the fresh id.

- [ ] **Step 8.9: Press during normal screen**

While in `IDLE`/`WORKING`, mash all three buttons. Confirm the screen does not flicker, serial shows no errors.

- [ ] **Step 8.10: Document test results**

If anything failed, file the failure mode in `docs/superpowers/specs/2026-04-27-permission-buttons-design.md` under a new "Open issues" section. If everything passes, no commit needed for this task.

---

## Final cleanup

- [ ] **Step F.1: Push the branch**

If working on a branch:
```sh
git push -u origin <branch>
```

Otherwise leave commits on `main`. Do not auto-push to `origin/main` without confirmation.
