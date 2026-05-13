# Device-side daily token cap — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Light up the usage bar on `StatusCard` against a *user-configured*
daily token budget, without depending on the bridge to evolve. Today the
bridge only emits the legacy `tokens_today` counter; the usage-remaining
work on `feature/usage-remaining-status` already added bar rendering for a
richer `usage` object that the bridge does not send. This plan closes the
gap from the firmware side: a new persisted `daily_token_cap` setting +
~15 lines of synthesis that fabricate a `ClaudeUsage` from
`(tokens_today, cap)` when the bridge hasn't supplied one.

**Architecture:**

- New `Settings` field `daily_token_cap` (`uint32_t`, tokens). `0` =
  disabled → legacy `tokens_today` line shown (current behavior).
  Non-zero → fake `ClaudeUsage{ used: tokens_today, limit: cap }` and
  let the existing bar renderer + `recompute_usage` derive remaining
  and `valid`.
- Bridge-supplied `usage` always wins: synthesis only runs when
  `status.usage.valid == false` *and* `cap > 0`. So if Anthropic ever
  exposes real quotas (or the bridge gains a config to send one), no
  firmware change is required.
- Synthesis lives in `lib/protocol/` as a pure helper so it's testable
  on the native env. StatusCard calls it once per render pass.
- Web UI gets one new number input in the Device section, validated
  against the same min/max as the settings model.

**Tech Stack:** ESP32-S3 (Arduino-ESP32 core), Unity native tests
(`pio test -e native`), existing `lib/settings` + `lib/protocol`
patterns, existing `core/EventBus.h` pub/sub, existing usage-bar render
path in `src/ui/cards/StatusCard.cpp`.

**Branch:** continue on `feature/usage-remaining-status` (already
contains the usage-bar render work). New commits are additive.

---

## File map

**Modified:**

- `lib/settings/settings_model.h` — one new field + constants
- `lib/settings/settings_model.cpp` — defaults, validator, JSON, new apply function
- `lib/protocol/protocol.h` — declare the synthesis helper
- `lib/protocol/protocol.cpp` — synthesis helper body
- `src/core/Settings.h` / `.cpp` — wrapper method `applyDailyCap`
- `src/ui/cards/StatusCard.cpp` — call synthesis in the render path
- `src/net/HttpServer.cpp` — extend Device form with the cap input
- `test/test_protocol/test_protocol.cpp` — synthesis tests
- `test/test_settings/test_settings.cpp` (or a new file alongside) —
  cap defaults / validation / JSON tests

**Created:** none. Everything bolts onto existing files.

**Test scope tradeoff:** the synthesis lives in `lib/protocol/` because
extracting it there is one extra function call but earns native test
coverage. The StatusCard render path itself (Arduino-coupled) is not
unit-tested — we rely on Task 8's hardware smoke and the fact that the
existing bar renderer is already exercised on device by the
`feature/usage-remaining-status` work. If the synthesis ever grows
non-trivial branching, extract a `UsageRender` helper and test that.

---

## Task 1: Settings model — add field + constants

**Files:**

- Modify: `lib/settings/settings_model.h`
- Modify: `lib/settings/settings_model.cpp` (`setDefaults`)
- Test: `test/test_settings/test_settings_daily_cap.cpp` (new — keeps the
  cap tests isolated from `test_settings.cpp` and
  `test_settings_backlight.cpp`)

- [ ] **Step 1: Failing test for defaults**

Create `test/test_settings/test_settings_daily_cap.cpp`:

```cpp
#include <unity.h>
#include <string.h>
#include "settings_model.h"

using namespace settings;

static Settings make_defaults() {
    Settings s = {};
    setDefaults(s, "Claude-AABB");
    return s;
}

static void test_daily_cap_default_zero(void) {
    Settings s = make_defaults();
    TEST_ASSERT_EQUAL_UINT32(0u, s.daily_token_cap);
}

extern "C" int run_daily_cap_settings_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_daily_cap_default_zero);
    return UNITY_END();
}
```

Then append a call to `run_daily_cap_settings_tests()` from the existing
`test/test_settings/test_settings.cpp` `main()` (or its single
`UNITY_BEGIN/END` block — match whatever pattern the other backlight
tests used).

- [ ] **Step 2: Run, confirm fail**

```
pio test -e native -f test_settings
```

Expected: compile error — `daily_token_cap` is not a member of `Settings`.

- [ ] **Step 3: Add field + constants**

In `lib/settings/settings_model.h`, near the existing backlight
constants, add:

```cpp
constexpr uint32_t DAILY_TOKEN_CAP_MAX = 100'000'000u;  // 100M, sanity ceiling
```

Inside `struct Settings`, after `full_level_pct`, add:

```cpp
    uint32_t daily_token_cap;     // 0 = disabled (legacy tokens_today line shown)
```

In `lib/settings/settings_model.cpp::setDefaults`, after
`s.full_level_pct = 100;`, add:

```cpp
    s.daily_token_cap = 0;
```

- [ ] **Step 4: Run, confirm pass**

```
pio test -e native -f test_settings
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/settings/settings_model.h lib/settings/settings_model.cpp \
        test/test_settings/test_settings_daily_cap.cpp \
        test/test_settings/test_settings.cpp   # if you wired up the runner there
git commit -m "feat(settings): add daily_token_cap field (default 0 = disabled)"
```

---

## Task 2: Settings model — validator

**Files:**

- Modify: `lib/settings/settings_model.cpp` (anonymous namespace + `validate`)
- Test: `test/test_settings/test_settings_daily_cap.cpp`

- [ ] **Step 1: Failing tests for validation rules**

Append to `test/test_settings/test_settings_daily_cap.cpp`:

```cpp
static void test_daily_cap_zero_accepted(void) {
    Settings s = make_defaults();
    s.daily_token_cap = 0;
    char err[64] = {};
    TEST_ASSERT_TRUE(validate(s, err, sizeof(err)));
}

static void test_daily_cap_in_range_accepted(void) {
    Settings s = make_defaults();
    s.daily_token_cap = 200'000u;
    char err[64] = {};
    TEST_ASSERT_TRUE(validate(s, err, sizeof(err)));
}

static void test_daily_cap_over_max_rejected(void) {
    Settings s = make_defaults();
    s.daily_token_cap = DAILY_TOKEN_CAP_MAX + 1;
    char err[64] = {};
    TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "daily_token_cap") != nullptr);
}
```

Add corresponding `RUN_TEST` lines.

- [ ] **Step 2: Run, confirm fail**

```
pio test -e native -f test_settings
```

Expected: the over-max test fails because no validation exists.

- [ ] **Step 3: Add the validator**

In `lib/settings/settings_model.cpp` anonymous namespace, alongside the
other `isValidXxx` helpers, add:

```cpp
bool isValidDailyTokenCap(uint32_t v, char* error, size_t error_len) {
    if (v > DAILY_TOKEN_CAP_MAX) {
        writeError(error, error_len,
                   "daily_token_cap out of range (0..100000000)");
        return false;
    }
    return true;
}
```

In the `validate` function, after the backlight validators, add:

```cpp
    if (!isValidDailyTokenCap(s.daily_token_cap, error, error_len)) return false;
```

- [ ] **Step 4: Run, confirm pass**

```
pio test -e native -f test_settings
```

Expected: all cap tests PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/settings/settings_model.cpp \
        test/test_settings/test_settings_daily_cap.cpp
git commit -m "feat(settings): validate daily_token_cap (0..100M)"
```

---

## Task 3: Settings model — `applyDailyCapField` + JSON output

**Files:**

- Modify: `lib/settings/settings_model.h` (declaration)
- Modify: `lib/settings/settings_model.cpp` (apply function + `toJson`)
- Test: `test/test_settings/test_settings_daily_cap.cpp`

- [ ] **Step 1: Failing test**

Append to `test/test_settings/test_settings_daily_cap.cpp`:

```cpp
static void test_apply_daily_cap_ok(void) {
    Settings s = make_defaults();
    char err[64] = {};
    TEST_ASSERT_TRUE(applyDailyCapField(s, 250'000u, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT32(250'000u, s.daily_token_cap);
}

static void test_apply_daily_cap_rejects_over_max(void) {
    Settings s = make_defaults();
    s.daily_token_cap = 1234u;
    char err[64] = {};
    TEST_ASSERT_FALSE(applyDailyCapField(s, DAILY_TOKEN_CAP_MAX + 1,
                                          err, sizeof(err)));
    // Unchanged on failure.
    TEST_ASSERT_EQUAL_UINT32(1234u, s.daily_token_cap);
}

static void test_tojson_includes_daily_cap(void) {
    Settings s = make_defaults();
    s.daily_token_cap = 12345u;
    char buf[1024] = {};
    size_t n = toJson(s, buf, sizeof(buf));
    TEST_ASSERT_NOT_EQUAL(0, n);
    TEST_ASSERT_TRUE(strstr(buf, "\"daily_token_cap\":12345") != nullptr);
}
```

Add `RUN_TEST` lines.

- [ ] **Step 2: Run, confirm fail**

```
pio test -e native -f test_settings
```

Expected: compile error (function not declared) and JSON test fails.

- [ ] **Step 3: Declare in header**

In `lib/settings/settings_model.h`, after the existing
`applyBacklightFields` declaration, add:

```cpp
// Patch with a new daily-token-cap value. Validates per the same rule as
// validate(). Returns true on success; false leaves s unchanged and
// writes the reason into error.
bool applyDailyCapField(Settings& s,
                        uint32_t daily_token_cap,
                        char* error, size_t error_len);
```

- [ ] **Step 4: Implement**

In `lib/settings/settings_model.cpp`, after `applyBacklightFields`, add:

```cpp
bool applyDailyCapField(Settings& s,
                        uint32_t daily_token_cap,
                        char* error, size_t error_len) {
    if (!isValidDailyTokenCap(daily_token_cap, error, error_len)) return false;
    s.daily_token_cap = daily_token_cap;
    return true;
}
```

- [ ] **Step 5: Extend `toJson`**

Locate the existing parent-object `snprintf` block (the one that emits
`device_name`, `live_timeout_s`, the backlight keys, `boot_card_id`,
etc.). Insert `"daily_token_cap":N,` between `full_level_pct` and
`boot_card_id`, matching the style of the surrounding keys. Append the
new value to the argument list in the same position. Keep the
trailing-comma discipline that the existing format string uses — no
extra trailing comma before `"cards":[`.

- [ ] **Step 6: Run, confirm pass**

```
pio test -e native -f test_settings
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add lib/settings/settings_model.h lib/settings/settings_model.cpp \
        test/test_settings/test_settings_daily_cap.cpp
git commit -m "feat(settings): applyDailyCapField + toJson includes daily_token_cap"
```

---

## Task 4: Settings wrapper — `applyDailyCap`

**Files:**

- Modify: `src/core/Settings.h` / `Settings.cpp`

> **No native test** — thin pass-through over `applyDailyCapField`.
> Exercised end-to-end by Task 7.

- [ ] **Step 1: Add declaration**

In `src/core/Settings.h`, after the existing `applyBacklight`
declaration, add:

```cpp
    // Applies a daily-token-cap update.
    bool applyDailyCap(uint32_t daily_token_cap,
                       char* err, size_t err_len);
```

- [ ] **Step 2: Add implementation**

In `src/core/Settings.cpp`, after `applyBacklight`, add:

```cpp
bool Settings::applyDailyCap(uint32_t daily_token_cap,
                             char* err, size_t err_len) {
    settings::Settings next = data_;
    if (!settings::applyDailyCapField(next, daily_token_cap, err, err_len)) {
        return false;
    }
    data_ = next;
    persist();
    if (bus_) bus_->publish(EventKind::SettingsChanged);
    return true;
}
```

- [ ] **Step 3: Build**

```
pio run -e adafruit_feather_esp32s3_reversetft
```

Expected: build success.

- [ ] **Step 4: Commit**

```bash
git add src/core/Settings.h src/core/Settings.cpp
git commit -m "feat(settings): wrapper Settings::applyDailyCap"
```

---

## Task 5: Synthesis helper in `lib/protocol/` + tests

**Files:**

- Modify: `lib/protocol/protocol.h` (new declaration)
- Modify: `lib/protocol/protocol.cpp` (new function body; `recompute_usage`
  stays `static`)
- Test: `test/test_protocol/test_protocol.cpp`

- [ ] **Step 1: Failing tests**

Append to `test/test_protocol/test_protocol.cpp`:

```cpp
static void test_synthesize_usage_disabled_when_cap_zero(void) {
    ClaudeUsage u = {};
    protocol_synthesize_usage_from_cap(/*used*/ 12000u, /*cap*/ 0u, &u);
    TEST_ASSERT_FALSE(u.valid);
}

static void test_synthesize_usage_fabricates_when_cap_set(void) {
    ClaudeUsage u = {};
    protocol_synthesize_usage_from_cap(/*used*/ 25000u, /*cap*/ 100000u, &u);
    TEST_ASSERT_TRUE(u.valid);
    TEST_ASSERT_EQUAL_UINT32(25000u,  u.used);
    TEST_ASSERT_EQUAL_UINT32(100000u, u.limit);
    TEST_ASSERT_EQUAL_UINT32(75000u,  u.remaining);
    TEST_ASSERT_TRUE(u.has_limit);
    TEST_ASSERT_FALSE(u.has_remaining);
}

static void test_synthesize_usage_clamps_overflow(void) {
    // tokens_today > cap is valid — the bar should clamp to 100% and the
    // synthesized usage should still be marked valid so the renderer
    // doesn't fall back to the legacy line on overshoot.
    ClaudeUsage u = {};
    protocol_synthesize_usage_from_cap(/*used*/ 150000u, /*cap*/ 100000u, &u);
    TEST_ASSERT_TRUE(u.valid);
    TEST_ASSERT_EQUAL_UINT32(150000u, u.used);
    TEST_ASSERT_EQUAL_UINT32(100000u, u.limit);
    // remaining stays 0 — limit < used path; the renderer will compute pct=100.
    TEST_ASSERT_EQUAL_UINT32(0u, u.remaining);
}
```

Add three `RUN_TEST` lines.

- [ ] **Step 2: Run, confirm fail**

```
pio test -e native -f test_protocol
```

Expected: compile error — function not declared.

- [ ] **Step 3: Declare in header**

In `lib/protocol/protocol.h`, after the `protocol_parse_line`
declaration, add:

```cpp
// Synthesize a ClaudeUsage from a (used, cap) pair when the bridge hasn't
// supplied one. cap == 0 leaves *out unchanged with valid=false. Used >
// cap is permitted — remaining is clamped to 0 and the caller's
// percent helper clamps to 100. *out must be zero-initialized or stale-
// safe; this function overwrites the fields it sets.
void protocol_synthesize_usage_from_cap(uint32_t used,
                                        uint32_t cap,
                                        ClaudeUsage* out);
```

- [ ] **Step 4: Implement**

In `lib/protocol/protocol.cpp`, after `protocol_parse_line`, add:

```cpp
void protocol_synthesize_usage_from_cap(uint32_t used,
                                        uint32_t cap,
                                        ClaudeUsage* out) {
    if (!out) return;
    if (cap == 0) {
        out->valid = false;
        return;
    }
    out->used          = used;
    out->limit         = cap;
    out->has_limit     = true;
    out->has_remaining = false;
    if (cap >= used) {
        out->remaining = cap - used;
    } else {
        out->remaining = 0;
    }
    out->valid = true;
}
```

> **Why not just call `recompute_usage`?** `recompute_usage` is `static`
> in this translation unit; exposing it would widen the API for one
> caller. Inlining the (limit - used) math keeps the helper
> self-contained and the test surface narrow.

- [ ] **Step 5: Run, confirm pass**

```
pio test -e native -f test_protocol
```

Expected: PASS (existing tests + 3 new).

- [ ] **Step 6: Commit**

```bash
git add lib/protocol/protocol.h lib/protocol/protocol.cpp \
        test/test_protocol/test_protocol.cpp
git commit -m "feat(protocol): synthesize ClaudeUsage from (tokens, cap) + tests"
```

---

## Task 6: StatusCard integration

**Files:**

- Modify: `src/ui/cards/StatusCard.h` (one new last-drawn field)
- Modify: `src/ui/cards/StatusCard.cpp`

> **No native test** — render path is Arduino-coupled. Verified by build
> and the smoke test in Task 8.

- [ ] **Step 1: Track the cap in the dirty check**

In `src/ui/cards/StatusCard.h`, near the other `last_drawn_*` fields
for usage, add:

```cpp
    uint32_t        last_drawn_daily_cap_;
```

Initialize it to `0xFFFFFFFFu` in the constructor initializer list
(matches the existing `last_drawn_usage_*` sentinels — guarantees the
first render path is "dirty").

- [ ] **Step 2: Synthesize + render**

Open `src/ui/cards/StatusCard.cpp`. The current render path reads
`status.usage` directly. We'll wrap that in a local `effective` value
that prefers the bridge-supplied usage and falls back to the cap-driven
synthesis.

In `StatusCard.cpp`, add at the top of the file (with other includes):

```cpp
#include "protocol.h"
#include "../../core/Settings.h"   // adjust path if needed
```

In the render method, replace each read of `status.usage` with a local
`effective`. Concretely, just before the existing usage-strip block
that begins:

```cpp
const uint8_t usage_pct = status.usage.valid ? usagePercent(status.usage) : 0;
```

add:

```cpp
ClaudeUsage effective = status.usage;
const uint32_t cap = state_.settings()
    ? state_.settings()->data().daily_token_cap : 0;
if (!effective.valid && cap > 0) {
    protocol_synthesize_usage_from_cap(status.tokens_today, cap, &effective);
}
```

> **Note:** if `AppState` does not already expose `settings()` on its
> public API, add a const getter that returns `const settings::Settings*`
> (or whatever wrapper type the rest of the code uses). The backlight
> work already depends on AppState carrying the settings pointer, so it
> is almost certainly present — confirm before adding.

Then replace every subsequent `status.usage` reference inside that block
(and below, including the dirty-track snapshot writes and the message-
block's `status.usage.valid` checks) with `effective`. Do not rename
`status` itself elsewhere.

Add the cap into the dirty check, so changing the cap via the web UI
triggers a repaint:

```cpp
const bool token_changed = state_changed ||
                           (last_drawn_valid_         != status.valid) ||
                           (last_drawn_tokens_today_  != status.tokens_today) ||
                           (last_drawn_usage_valid_   != effective.valid) ||
                           (last_drawn_usage_used_    != effective.used) ||
                           (last_drawn_usage_remaining_ != effective.remaining) ||
                           (effective.valid && last_drawn_usage_pct_ != usage_pct) ||
                           (last_drawn_daily_cap_     != cap);
```

In the snapshot-write block at the end of the render method, after
`last_drawn_usage_pct_ = usage_pct;`, add:

```cpp
last_drawn_daily_cap_ = cap;
```

> **Why also track the cap and not just rely on the synthesized
> `effective`?** When the cap goes from `0` to non-zero (or vice versa)
> while `tokens_today` is unchanged, the synthesized values may differ
> only in `valid`. The dirty check already covers `effective.valid`, so
> in practice the cap field is belt-and-braces — but it's free, and it
> makes the intent ("the cap changed") explicit at the call site.

- [ ] **Step 3: Build**

```
pio run -e adafruit_feather_esp32s3_reversetft
```

Expected: build success. Failures here are almost always one of:

- `state_.settings()` doesn't exist or has a different name — adjust to
  whatever AppState actually exposes.
- A `status.usage.*` reference deeper in the render method was missed —
  finish renaming.

- [ ] **Step 4: Commit**

```bash
git add src/ui/cards/StatusCard.h src/ui/cards/StatusCard.cpp
git commit -m "feat(StatusCard): synthesize usage from daily_token_cap when bridge silent"
```

---

## Task 7: HttpServer — expose `daily_token_cap` on the Device form

**Files:**

- Modify: `src/net/HttpServer.cpp`

- [ ] **Step 1: Extend the Device form HTML**

Locate the Device section in the form (around the dim/sleep timeout
inputs the backlight work added). After the `Full level (%)` form-group,
insert:

```cpp
                "<div class=form-group>"
                  "<label for=dtc>Daily token cap</label>"
                  "<input id=dtc name=daily_token_cap type=number min=0 max=100000000 required>"
                  "<p class=tip>If &gt; 0, the status card shows a usage bar against this daily token budget. 0 hides the bar and shows the legacy tokens-today line.</p>"
                "</div>"
```

- [ ] **Step 2: Pre-fill in `loadSettings`**

Find the JS `loadSettings` block. After the line that sets the full
level input from `s.full_level_pct`, add:

```cpp
              "$('dtc').value=s.daily_token_cap;"
```

- [ ] **Step 3: Submit the new field**

The existing `saveDevice` handler builds its request body from
`new FormData(form)` (or a manual string — confirm the style). The new
input has a `name` attribute, so `FormData` picks it up automatically.
If a manual body is built, append `&daily_token_cap=${$('dtc').value}`
in the same pattern as the existing backlight fields.

- [ ] **Step 4: Server-side handler**

Find the `/api/settings/device` POST handler (the one extended in the
backlight work). Add:

```cpp
        if (!server_->hasArg("daily_token_cap")) {
            sendJsonError(server_, 400, "missing daily_token_cap");
            return;
        }
        long dtc = server_->arg("daily_token_cap").toInt();
        if (dtc < 0 || dtc > 0xFFFFFFFFL) {
            sendJsonError(server_, 400, "daily_token_cap out of range");
            return;
        }
```

After the existing `applyBacklight` call, add:

```cpp
        if (!settings_.applyDailyCap(static_cast<uint32_t>(dtc),
                                     err, sizeof(err))) {
            sendJsonError(server_, 400, err);
            return;
        }
```

> **Note on atomicity:** like the backlight wiring, if `applyDailyCap`
> rejects after `applyDevice`/`applyBacklight` already persisted, the
> earlier writes stay. Acceptable for the same reason — the form-level
> validation makes simultaneous failure of multiple sections rare in
> practice, and a re-submit pushes everything cleanly.

- [ ] **Step 5: Build + upload**

```
pio run -e adafruit_feather_esp32s3_reversetft -t upload
```

Expected: build + upload success.

- [ ] **Step 6: Smoke-test the form in a browser**

- Open `http://<device-ip>/`.
- Verify the Device section now lists `Daily token cap`, pre-filled
  with `0`.
- Save the form with `daily_token_cap = 100000`. Expect `200 OK`.
- Save with `daily_token_cap = 100000001` → expect `400` with the
  range error message.
- Save back to `0` → expect `200 OK`.

- [ ] **Step 7: Commit**

```bash
git add src/net/HttpServer.cpp
git commit -m "feat(http): expose daily_token_cap on the Device form"
```

---

## Task 8: Hardware smoke test

**No code changes; acceptance gate.** Run on the physical Feather.

- [ ] **Step 1: Flash and connect**

```
pio run -e adafruit_feather_esp32s3_reversetft -t upload
pio device monitor
```

Pair the device over BLE with the Claude Code desktop's device-buddy
bridge so it starts receiving heartbeat snapshots with `tokens_today`.

- [ ] **Step 2: Verify cap=0 keeps legacy behavior**

In the web UI, set `daily_token_cap = 0`. The Status card should show
the existing `NNK tokens today` line — no bar, no percentage. This
proves the firmware doesn't regress on existing devices that haven't
opted in to the new feature.

- [ ] **Step 3: Verify a real cap lights up the bar**

Set `daily_token_cap = 50000` and save. The Status card should
immediately repaint:

- Top line: `NN% used` (large white text). For `tokens_today=12400`,
  expect `25% used`.
- Below it: an 8-px progress bar at y=85, filled to ~25 % width, color
  cyan (because pct < 45).
- Right-aligned: `37.6K left` (or whatever `format_token_count` does
  for `50000 - 12400`).

Move the cap to `15000`. Bar should jump to ~83 % filled, color yellow
(70..89). Move to `12500` → ~99 %, color red (≥90). Move to `5000` →
bar should clamp at 100 %, red, `0K left`.

- [ ] **Step 4: Verify settings persistence**

Power-cycle. After reboot, the cap you last set must round-trip — open
the web UI and check the input still shows the saved value.

- [ ] **Step 5: Verify bridge-supplied usage still wins (if reachable)**

If you can send a snapshot containing a `usage` object from the bridge
(or from a test fixture you can flip on), confirm the device renders
the bridge-supplied bar and *not* the cap-derived one. Easiest way to
prove without bridge work: temporarily flip
`!effective.valid && cap > 0` to `cap > 0` in StatusCard, observe the
cap-derived bar overriding a synthetic bridge-supplied usage in a test
build, then revert. (Skip if not practical — bridge precedence is also
covered by the parser tests, which set `usage.valid = true` and the
render path's existing `effective = status.usage` line.)

- [ ] **Step 6: Commit a hardware-test note**

If everything passes, no commit needed. If anything fails, open a
follow-up issue and revert the offending task before merging.

---

## Acceptance criteria

- `daily_token_cap = 0` (default) → existing `tokens_today` line shown,
  no behavior change for users upgrading from the pre-cap firmware.
- `daily_token_cap > 0` and bridge sending no `usage` object → bar
  renders with percentage = `tokens_today * 100 / cap`, remaining =
  `cap - tokens_today`, color thresholds as per the existing renderer.
- `tokens_today > daily_token_cap` → bar clamps at 100 %, color red,
  `0K left`.
- Bridge later starts sending a real `usage` object → that always wins
  over the cap-derived synthesis (synthesis only runs when
  `status.usage.valid` is false).
- Cap changes via the web UI repaint the bar without requiring a
  reboot.
- Native test suite passes:
  - `pio test -e native -f test_settings` — defaults, validation,
    apply, JSON round-trip.
  - `pio test -e native -f test_protocol` — synthesis helper covers
    cap=0, cap>used, cap<used.
- ESP32 firmware builds and uploads cleanly.

## Follow-up (out of scope)

- Weekly cap (`weekly_token_cap`) — needs a `tokens_week` field that
  the bridge doesn't currently emit.
- Reset-time display ("resets in 4h 12m") — needs a quota reset
  timestamp from somewhere.
- Audible/visual alarm near exhaustion — the existing color thresholds
  already escalate red ≥90 %; explicit alarms are a separate decision.
- Move the cap input out of the Device section if/when a dedicated
  "Usage" section accumulates more knobs.
