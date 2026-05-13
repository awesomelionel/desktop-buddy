# Bus Arrivals Card Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a family of up to four sibling cards to Claude Buddy that show next-bus arrival times for user-configured Singapore bus stops, fetched from `https://api.busaunty.com/api/v1/BusArrival` every 30 s while the card is visible.

**Architecture:** Pure-C++ parse library lives in `lib/bus_arrivals/` and is host-tested via PlatformIO's native env. The Arduino-only fetcher in `src/net/BusArrivalsFetcher.{h,cpp}` does the HTTPClient + parse + bound-checks. One `BusCard` class in `src/ui/cards/` is instantiated four times in `CardController` (one per slot 0..3), each with its own fetch state and 30-s polling cadence that only runs while the card is the visible top of the stack. Settings gain a `bus_stops[4]` array with `code`+`label`; the NVS storage key bumps from `"v1"` to `"v2"` with a one-shot legacy-blob migration.

**Tech Stack:** C++17 / Arduino-ESP32 (pioarduino fork, v3.x), Adafruit ST7789 + Adafruit_GFX, ArduinoJson v7 (with `Filter`), Arduino `HTTPClient` + `NetworkClientSecure`, Unity test framework (PlatformIO native env).

**Reference spec:** `docs/superpowers/specs/2026-05-13-bus-arrivals-card-design.md` (commit `fb2dde1`).

---

## File Structure

| Path | Status | Responsibility |
|---|---|---|
| `lib/settings/settings_model.h` | modify | Add `BusStopSlot`, `bus_stops[4]`, `CARD_BUS_1..4`, validation/apply prototypes |
| `lib/settings/settings_model.cpp` | modify | Implement `applyBusStopField`, validation, `setDefaults`, `toJson`, `cardName` updates |
| `src/core/Settings.h` | modify | Add `applyBusStop()` method declaration |
| `src/core/Settings.cpp` | modify | Implement `applyBusStop`; bump NVS KEY `"v1"`→`"v2"` with legacy-blob migration |
| `lib/bus_arrivals/bus_arrivals.h` | create | Public types (`BusLoad`, `BusType`, `BusServiceArrival`, `BusStopArrivals`); `parseIso8601Delta`, `parseBusArrivalsJson` declarations |
| `lib/bus_arrivals/bus_arrivals.cpp` | create | `parseIso8601Delta` (sscanf-based), `parseBusArrivalsJson` (ArduinoJson + Filter) |
| `lib/bus_arrivals/library.json` | create | PlatformIO library manifest |
| `test/test_bus_arrivals/test_bus_arrivals.cpp` | create | Unity tests for the parse library |
| `test/test_settings/test_settings_bus_stops.cpp` | create | Unity tests for the bus-stop settings additions |
| `test/test_settings/test_settings.cpp` | modify | Register the new bus-stop tests in `main()` |
| `src/ui/Card.h` | modify | Add `virtual void onShow() {}` and `virtual void onHide() {}` defaults |
| `src/ui/CardStack.h` | modify | (no API change; doc only) |
| `src/ui/CardStack.cpp` | modify | Call `onHide()` on prior top, `onShow()` on new top, on every `setIndex/next/prev/pushOverlay/clearOverlay` |
| `src/net/BusArrivalsFetcher.h` | create | Class declaration |
| `src/net/BusArrivalsFetcher.cpp` | create | HTTPClient + bound-checked stream parse |
| `src/ui/cards/BusCard.h` | create | `BusCard` class declaration |
| `src/ui/cards/BusCard.cpp` | create | `BusCard` lifecycle + render + tick + button handling |
| `src/ui/CardController.h` | modify | Add four `BusCard` members |
| `src/ui/CardController.cpp` | modify | Construct the four `BusCard`s; extend `cardForId` and `rebuildStack` filtering |
| `src/net/HttpServer.cpp` | modify | Extend HTML form, extend `/api/settings` JSON, add `POST /api/settings/bus-stops` |

No new platformio.ini lib_deps required (ArduinoJson is already in `lib_deps`; the new pure-C++ library is auto-discovered from `lib/` per `lib_ldf_mode = deep`).

---

## Conventions used in this plan

- **Build & flash a single env:** `pio run -e adafruit_feather_esp32s3_reversetft` (no upload needed for pure compile checks). To upload, append `-t upload`.
- **Run all native tests:** `pio test -e native`.
- **Run a single native test suite:** `pio test -e native -f test_<name>` (e.g. `pio test -e native -f test_bus_arrivals`).
- **Each task ends with a commit.** Use the suggested message verbatim or adjust for accuracy.
- **TDD where testable** (settings model, parse library, ISO parser). Render code and HTTPClient wiring are verified by build + on-device manual checklist (Task 26).

---

## Task 1: Extend `settings_model.h` with bus-stop schema

**Files:**
- Modify: `lib/settings/settings_model.h`

- [ ] **Step 1: Add new constants and `BusStopSlot` struct**

In `lib/settings/settings_model.h`, after the existing `DAILY_TOKEN_CAP_MAX` constant (around line 29), add:

```cpp
constexpr uint8_t  MAX_BUS_STOPS       = 4;
constexpr uint8_t  MAX_BUS_LABEL_LEN   = 12;   // not counting null
constexpr uint8_t  BUS_STOP_CODE_LEN   = 5;    // Singapore stop codes are exactly 5 digits

struct BusStopSlot {
    char code[BUS_STOP_CODE_LEN + 1];   // "" = empty slot, disables card
    char label[MAX_BUS_LABEL_LEN + 1];  // "" = renderer falls back to "Stop NNNNN"
};
```

- [ ] **Step 2: Extend the `CardId` enum**

Replace the existing `CardId` enum body so it reads:

```cpp
enum CardId : uint8_t {
    CARD_STATUS  = 0,
    CARD_EYES    = 1,
    CARD_WIFI    = 2,
    CARD_NAVTEST = 3,
    CARD_BUS_1   = 4,
    CARD_BUS_2   = 5,
    CARD_BUS_3   = 6,
    CARD_BUS_4   = 7,
    CARD_COUNT   = 8,  // sentinel; not a real card
};
```

- [ ] **Step 3: Add `bus_stops[]` field to `Settings`**

Inside `struct Settings`, add as the last field (after `boot_card_id`):

```cpp
BusStopSlot bus_stops[MAX_BUS_STOPS];
```

- [ ] **Step 4: Declare `applyBusStopField`**

After the existing `applyDailyCapField` declaration, add:

```cpp
// Patch a single bus stop slot. slot must be < MAX_BUS_STOPS.
// code may be "" (clears the slot and auto-disables the corresponding card)
// or exactly BUS_STOP_CODE_LEN ASCII digits. label may be empty or up to
// MAX_BUS_LABEL_LEN printable ASCII chars. Returns true on success;
// false leaves s unchanged and writes the reason into error.
bool applyBusStopField(Settings& s,
                       uint8_t slot,
                       const char* code,
                       const char* label,
                       char* error, size_t error_len);
```

- [ ] **Step 5: Build to confirm header still compiles transitively**

Run: `pio run -e adafruit_feather_esp32s3_reversetft -t checkprogsize 2>&1 | tail -20`

Expected: build succeeds (header changes alone won't link; existing `setDefaults`/`validate` references to `cards_order[CARD_COUNT]` still type-check at the new size of 8 because `cards_order` was already declared as `uint8_t cards_order[CARD_COUNT]`).

If a compile error mentions an out-of-range index in any consumer, defer fixing it — the next tasks will adjust `setDefaults`, `validate`, `toJson`, and the consumers explicitly.

- [ ] **Step 6: Commit**

```bash
git add lib/settings/settings_model.h
git commit -m "feat(settings): extend schema for bus-stop slots and CARD_BUS_1..4"
```

---

## Task 2: Add bus-stop validation & apply helper, with tests

**Files:**
- Create: `test/test_settings/test_settings_bus_stops.cpp`
- Modify: `lib/settings/settings_model.cpp`
- Modify: `test/test_settings/test_settings.cpp`

- [ ] **Step 1: Write the failing tests**

Create `test/test_settings/test_settings_bus_stops.cpp` with the full test set:

```cpp
#include <unity.h>
#include <string.h>
#include "settings_model.h"

using namespace settings;

static Settings make_defaults_bus() {
    Settings s = {};
    setDefaults(s, "Claude-AABB");
    return s;
}

void test_bus_stops_default_all_empty(void) {
    Settings s = make_defaults_bus();
    for (uint8_t i = 0; i < MAX_BUS_STOPS; ++i) {
        TEST_ASSERT_EQUAL_STRING("", s.bus_stops[i].code);
        TEST_ASSERT_EQUAL_STRING("", s.bus_stops[i].label);
    }
}

void test_apply_bus_stop_valid_code_accepted(void) {
    Settings s = make_defaults_bus();
    char err[64] = {};
    TEST_ASSERT_TRUE(applyBusStopField(s, 0, "50171", "Home",
                                       err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("50171", s.bus_stops[0].code);
    TEST_ASSERT_EQUAL_STRING("Home",  s.bus_stops[0].label);
}

void test_apply_bus_stop_empty_code_clears_slot_and_label(void) {
    Settings s = make_defaults_bus();
    char err[64] = {};
    TEST_ASSERT_TRUE(applyBusStopField(s, 0, "50171", "Home",
                                       err, sizeof(err)));
    TEST_ASSERT_TRUE(applyBusStopField(s, 0, "", "ignored",
                                       err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("", s.bus_stops[0].code);
    TEST_ASSERT_EQUAL_STRING("", s.bus_stops[0].label);
}

void test_apply_bus_stop_empty_code_clears_card_mask_bit(void) {
    Settings s = make_defaults_bus();
    s.cards_enabled_mask |= (1u << CARD_BUS_2);
    char err[64] = {};
    TEST_ASSERT_TRUE(applyBusStopField(s, 1, "54321", "Office",
                                       err, sizeof(err)));
    // Now clear it. The CARD_BUS_2 bit must be auto-cleared.
    TEST_ASSERT_TRUE(applyBusStopField(s, 1, "", "",
                                       err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT8(0u, s.cards_enabled_mask & (1u << CARD_BUS_2));
}

void test_apply_bus_stop_non_digit_rejected(void) {
    Settings s = make_defaults_bus();
    s.bus_stops[0].code[0]  = '\0';
    char err[64] = {};
    TEST_ASSERT_FALSE(applyBusStopField(s, 0, "5017a", "Home",
                                        err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "code") != nullptr);
    TEST_ASSERT_EQUAL_STRING("", s.bus_stops[0].code);
}

void test_apply_bus_stop_wrong_length_rejected(void) {
    Settings s = make_defaults_bus();
    char err[64] = {};
    TEST_ASSERT_FALSE(applyBusStopField(s, 0, "501",   "X", err, sizeof(err)));
    TEST_ASSERT_FALSE(applyBusStopField(s, 0, "501712", "X", err, sizeof(err)));
}

void test_apply_bus_stop_label_too_long_rejected(void) {
    Settings s = make_defaults_bus();
    char err[64] = {};
    // MAX_BUS_LABEL_LEN is 12, so a 13-char label is too long.
    TEST_ASSERT_FALSE(applyBusStopField(s, 0, "50171", "Thirteenchars",
                                        err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "label") != nullptr);
}

void test_apply_bus_stop_label_with_control_char_rejected(void) {
    Settings s = make_defaults_bus();
    char err[64] = {};
    char bad_label[] = {'H','o','m','\x01','e','\0'};
    TEST_ASSERT_FALSE(applyBusStopField(s, 0, "50171", bad_label,
                                        err, sizeof(err)));
}

void test_apply_bus_stop_slot_out_of_range_rejected(void) {
    Settings s = make_defaults_bus();
    char err[64] = {};
    TEST_ASSERT_FALSE(applyBusStopField(s, MAX_BUS_STOPS, "50171", "X",
                                        err, sizeof(err)));
}

void test_validate_accepts_default_settings(void) {
    Settings s = make_defaults_bus();
    char err[64] = {};
    TEST_ASSERT_TRUE(validate(s, err, sizeof(err)));
}

void test_validate_rejects_card_enabled_with_empty_code(void) {
    Settings s = make_defaults_bus();
    // Force a bus card bit on without setting its code.
    s.cards_enabled_mask |= (1u << CARD_BUS_1);
    s.cards_order[s.cards_order_count++] = CARD_BUS_1;
    char err[64] = {};
    TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "bus") != nullptr);
}
```

- [ ] **Step 2: Register the new tests in the test runner**

In `test/test_settings/test_settings.cpp`, after the existing `// Defined in test_settings_daily_cap.cpp` block (around line 33), add:

```cpp
// Defined in test_settings_bus_stops.cpp
void test_bus_stops_default_all_empty(void);
void test_apply_bus_stop_valid_code_accepted(void);
void test_apply_bus_stop_empty_code_clears_slot_and_label(void);
void test_apply_bus_stop_empty_code_clears_card_mask_bit(void);
void test_apply_bus_stop_non_digit_rejected(void);
void test_apply_bus_stop_wrong_length_rejected(void);
void test_apply_bus_stop_label_too_long_rejected(void);
void test_apply_bus_stop_label_with_control_char_rejected(void);
void test_apply_bus_stop_slot_out_of_range_rejected(void);
void test_validate_accepts_default_settings(void);
void test_validate_rejects_card_enabled_with_empty_code(void);
```

Then in `main()` (around line 213, after the last `RUN_TEST` for daily-cap tests, but **before** the closing `return UNITY_END();`), add:

```cpp
    RUN_TEST(test_bus_stops_default_all_empty);
    RUN_TEST(test_apply_bus_stop_valid_code_accepted);
    RUN_TEST(test_apply_bus_stop_empty_code_clears_slot_and_label);
    RUN_TEST(test_apply_bus_stop_empty_code_clears_card_mask_bit);
    RUN_TEST(test_apply_bus_stop_non_digit_rejected);
    RUN_TEST(test_apply_bus_stop_wrong_length_rejected);
    RUN_TEST(test_apply_bus_stop_label_too_long_rejected);
    RUN_TEST(test_apply_bus_stop_label_with_control_char_rejected);
    RUN_TEST(test_apply_bus_stop_slot_out_of_range_rejected);
    RUN_TEST(test_validate_accepts_default_settings);
    RUN_TEST(test_validate_rejects_card_enabled_with_empty_code);
```

- [ ] **Step 3: Run tests and confirm they fail to link**

Run: `pio test -e native -f test_settings 2>&1 | tail -30`

Expected: link error mentioning `applyBusStopField` undefined (it's not implemented yet). This confirms the harness is wired correctly.

- [ ] **Step 4: Implement `applyBusStopField` and adjust `validate` + `setDefaults`**

In `lib/settings/settings_model.cpp`:

(a) Add a helper near the top of the anonymous namespace (after `isPrintableAscii`, around line 22):

```cpp
bool isAsciiDigit(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    return u >= '0' && u <= '9';
}

bool isValidBusStopCode(const char* code, char* error, size_t error_len) {
    if (!code) { writeError(error, error_len, "bus_stops code missing"); return false; }
    size_t n = strlen(code);
    if (n == 0) return true;                       // empty = disabled slot
    if (n != BUS_STOP_CODE_LEN) {
        writeError(error, error_len, "bus_stops code must be 5 digits or empty");
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        if (!isAsciiDigit(code[i])) {
            writeError(error, error_len, "bus_stops code must be 5 digits or empty");
            return false;
        }
    }
    return true;
}

bool isValidBusStopLabel(const char* label, char* error, size_t error_len) {
    if (!label) { writeError(error, error_len, "bus_stops label missing"); return false; }
    size_t n = strlen(label);
    if (n > MAX_BUS_LABEL_LEN) {
        writeError(error, error_len, "bus_stops label too long (max 12)");
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        if (!isPrintableAscii(label[i])) {
            writeError(error, error_len, "bus_stops label has non-printable chars");
            return false;
        }
    }
    return true;
}

bool isCardBusId(uint8_t id) {
    return id >= CARD_BUS_1 && id <= CARD_BUS_4;
}

uint8_t busSlotForCardId(uint8_t id) {
    return (uint8_t)(id - CARD_BUS_1);
}
```

(b) In `setDefaults` (around line 146), after `s.boot_card_id = CARD_STATUS;`, add:

```cpp
    for (uint8_t i = 0; i < MAX_BUS_STOPS; ++i) {
        s.bus_stops[i].code[0]  = '\0';
        s.bus_stops[i].label[0] = '\0';
    }
```

(c) In `validate` (around line 171), **after** the existing `boot_card_id` validation block but **before** the final `return true;`, add:

```cpp
    for (uint8_t i = 0; i < MAX_BUS_STOPS; ++i) {
        if (!isValidBusStopCode(s.bus_stops[i].code,   error, error_len)) return false;
        if (!isValidBusStopLabel(s.bus_stops[i].label, error, error_len)) return false;
    }
    // Reject any enabled bus card whose corresponding slot is empty.
    for (uint8_t id = CARD_BUS_1; id <= CARD_BUS_4; ++id) {
        if ((s.cards_enabled_mask & (1u << id)) == 0) continue;
        if (s.bus_stops[busSlotForCardId(id)].code[0] == '\0') {
            writeError(error, error_len, "bus card enabled with empty code");
            return false;
        }
    }
```

(d) Implement `applyBusStopField` (add at the end of the file, before the closing `}  // namespace settings`):

```cpp
bool applyBusStopField(Settings& s,
                       uint8_t slot,
                       const char* code,
                       const char* label,
                       char* error, size_t error_len) {
    if (slot >= MAX_BUS_STOPS) {
        writeError(error, error_len, "bus_stops slot out of range");
        return false;
    }
    if (!isValidBusStopCode(code,   error, error_len)) return false;
    // When clearing the slot, ignore the supplied label entirely.
    const bool clearing = (code[0] == '\0');
    if (!clearing) {
        if (!isValidBusStopLabel(label, error, error_len)) return false;
    }

    // Apply.
    if (clearing) {
        s.bus_stops[slot].code[0]  = '\0';
        s.bus_stops[slot].label[0] = '\0';
        // Auto-clear the corresponding card mask bit so validate() stays happy.
        const uint8_t card_id = (uint8_t)(CARD_BUS_1 + slot);
        if (s.cards_enabled_mask & (1u << card_id)) {
            s.cards_enabled_mask &= (uint8_t)~(1u << card_id);
            // Drop the card id from cards_order if present.
            uint8_t out = 0;
            for (uint8_t i = 0; i < s.cards_order_count; ++i) {
                if (s.cards_order[i] != card_id) {
                    s.cards_order[out++] = s.cards_order[i];
                }
            }
            // Zero the tail.
            for (uint8_t i = out; i < CARD_COUNT; ++i) s.cards_order[i] = 0;
            s.cards_order_count = out;
            // If boot_card_id was the cleared card, drop to first enabled.
            if (s.boot_card_id == card_id) {
                for (uint8_t i = 0; i < CARD_COUNT; ++i) {
                    if (s.cards_enabled_mask & (1u << i)) {
                        s.boot_card_id = i;
                        break;
                    }
                }
            }
        }
    } else {
        size_t n = strlen(code);
        memcpy(s.bus_stops[slot].code, code, n);
        s.bus_stops[slot].code[n] = '\0';
        size_t m = strlen(label);
        memcpy(s.bus_stops[slot].label, label, m);
        s.bus_stops[slot].label[m] = '\0';
    }
    return true;
}
```

- [ ] **Step 5: Run tests and confirm they pass**

Run: `pio test -e native -f test_settings 2>&1 | tail -20`

Expected: all settings tests pass (existing 30+ plus the 11 new ones).

- [ ] **Step 6: Commit**

```bash
git add lib/settings/settings_model.h lib/settings/settings_model.cpp \
        test/test_settings/test_settings_bus_stops.cpp \
        test/test_settings/test_settings.cpp
git commit -m "feat(settings): validate bus-stop codes/labels and applyBusStopField"
```

---

## Task 3: Extend `toJson` to include `bus_stops` array

**Files:**
- Modify: `lib/settings/settings_model.cpp`
- Modify: `lib/settings/settings_model.h`
- Modify: `test/test_settings/test_settings_bus_stops.cpp`
- Modify: `test/test_settings/test_settings.cpp`

- [ ] **Step 1: Add the failing test**

Append to `test/test_settings/test_settings_bus_stops.cpp`:

```cpp
void test_tojson_includes_bus_stops_array(void) {
    Settings s = make_defaults_bus();
    char err[64] = {};
    TEST_ASSERT_TRUE(applyBusStopField(s, 0, "50171", "Home",
                                       err, sizeof(err)));
    TEST_ASSERT_TRUE(applyBusStopField(s, 2, "54321", "",
                                       err, sizeof(err)));
    char buf[2048] = {};
    size_t n = toJson(s, buf, sizeof(buf));
    TEST_ASSERT_NOT_EQUAL(0, n);
    TEST_ASSERT_TRUE(strstr(buf, "\"bus_stops\":[") != nullptr);
    TEST_ASSERT_TRUE(strstr(buf, "\"slot\":0") != nullptr);
    TEST_ASSERT_TRUE(strstr(buf, "\"code\":\"50171\"") != nullptr);
    TEST_ASSERT_TRUE(strstr(buf, "\"label\":\"Home\"") != nullptr);
    TEST_ASSERT_TRUE(strstr(buf, "\"code\":\"54321\"") != nullptr);
    TEST_ASSERT_TRUE(strstr(buf, "\"slot\":3") != nullptr);  // empty slot still emitted
}

void test_tojson_includes_bus_card_names(void) {
    Settings s = make_defaults_bus();
    char buf[2048] = {};
    size_t n = toJson(s, buf, sizeof(buf));
    TEST_ASSERT_NOT_EQUAL(0, n);
    TEST_ASSERT_TRUE(strstr(buf, "\"name\":\"Bus 1\"") != nullptr);
    TEST_ASSERT_TRUE(strstr(buf, "\"name\":\"Bus 4\"") != nullptr);
}
```

In `test/test_settings/test_settings.cpp`, declare and register:

```cpp
void test_tojson_includes_bus_stops_array(void);
void test_tojson_includes_bus_card_names(void);
```

```cpp
    RUN_TEST(test_tojson_includes_bus_stops_array);
    RUN_TEST(test_tojson_includes_bus_card_names);
```

- [ ] **Step 2: Run, expect failure**

Run: `pio test -e native -f test_settings 2>&1 | tail -20`

Expected: the two new tests fail (cards array uses old `cardName` for unknown ids, and `bus_stops` key missing).

- [ ] **Step 3: Update `cardName` for the new bus IDs**

In `lib/settings/settings_model.cpp`, replace the `cardName` switch (around line 265) so it covers all eight ids:

```cpp
const char* cardName(CardId id) {
    switch (id) {
        case CARD_STATUS:  return "Status";
        case CARD_EYES:    return "Eyes";
        case CARD_WIFI:    return "Wifi";
        case CARD_NAVTEST: return "NavTest";
        case CARD_BUS_1:   return "Bus 1";
        case CARD_BUS_2:   return "Bus 2";
        case CARD_BUS_3:   return "Bus 3";
        case CARD_BUS_4:   return "Bus 4";
        case CARD_COUNT:   return "?";
    }
    return "?";
}
```

- [ ] **Step 4: Extend `toJson` to emit `bus_stops`**

In `lib/settings/settings_model.cpp`, find the trailing `int n = snprintf(buf + pos, buf_len - pos, "]}");` (around line 331). Replace it with:

```cpp
    int n = snprintf(buf + pos, buf_len - pos, "],\"bus_stops\":[");
    if (n < 0 || (size_t)n >= buf_len - pos) return 0;
    pos += (size_t)n;

    for (uint8_t i = 0; i < MAX_BUS_STOPS; ++i) {
        const char* sep_b = (i == 0) ? "" : ",";
        n = snprintf(buf + pos, buf_len - pos,
            "%s{\"slot\":%u,\"code\":\"%s\",\"label\":\"%s\"}",
            sep_b,
            (unsigned)i,
            s.bus_stops[i].code,
            s.bus_stops[i].label);
        if (n < 0 || (size_t)n >= buf_len - pos) return 0;
        pos += (size_t)n;
    }

    n = snprintf(buf + pos, buf_len - pos, "]}");
    if (n < 0 || (size_t)n >= buf_len - pos) return 0;
    pos += (size_t)n;
    return pos;
```

(Note: the existing comment in the spec about JSON shape was a sketch; this is the canonical shape — `bus_stops` always has exactly `MAX_BUS_STOPS` entries, with `code: ""` for empty slots so the web UI can render all four slot rows from one fetch.)

Also update the `// Shape:` doc comment at the top of `toJson`'s declaration in `lib/settings/settings_model.h` (around line 88) to:

```cpp
// Render Settings as a single JSON object into buf. Returns the number of
// chars written (excluding null), or 0 if buf_len is too small.
//
// Shape:
//   {"device_name":"...","live_timeout_s":N,"sleep_timeout_s":N,
//    "dim_timeout_s":N,"dim_level_pct":N,"full_level_pct":N,
//    "daily_token_cap":N,"boot_card_id":N,
//    "cards":[{"id":N,"name":"...","enabled":bool,"order":N}, ...],
//    "bus_stops":[{"slot":N,"code":"...","label":"..."}, ...]}
```

- [ ] **Step 5: Run, expect pass**

Run: `pio test -e native -f test_settings 2>&1 | tail -20`

Expected: all settings tests pass.

- [ ] **Step 6: Commit**

```bash
git add lib/settings/settings_model.h lib/settings/settings_model.cpp \
        test/test_settings/test_settings_bus_stops.cpp \
        test/test_settings/test_settings.cpp
git commit -m "feat(settings): include bus_stops in toJson and name new bus cards"
```

---

## Task 4: Bump NVS storage key with legacy migration

**Files:**
- Modify: `src/core/Settings.h`
- Modify: `src/core/Settings.cpp`

There is no native test for this (NVS is hardware-only). The migration is a small, self-contained change verified by build + on-device boot in Task 26.

- [ ] **Step 1: Add `applyBusStop` declaration in `Settings.h`**

In `src/core/Settings.h`, after the `applyDailyCap` declaration (around line 47), add:

```cpp
    // Applies a per-slot bus-stop update. Returns true on success;
    // on failure, err is populated and in-memory state is unchanged.
    bool applyBusStop(uint8_t slot,
                      const char* code,
                      const char* label,
                      char* err, size_t err_len);
```

- [ ] **Step 2: Implement `applyBusStop`**

In `src/core/Settings.cpp`, after `applyDailyCap` (around line 92), add:

```cpp
bool Settings::applyBusStop(uint8_t slot,
                            const char* code,
                            const char* label,
                            char* err, size_t err_len) {
    settings::Settings next = data_;
    if (!settings::applyBusStopField(next, slot, code, label, err, err_len)) {
        return false;
    }
    data_ = next;
    persist();
    if (bus_) bus_->publish(EventKind::SettingsChanged);
    return true;
}
```

- [ ] **Step 3: Bump NVS key and add legacy-blob migration**

In `src/core/Settings.cpp`, replace the `KEY` constant and the body of `Settings::begin` (lines 9-35) with:

```cpp
namespace {
constexpr const char* NS       = "settings";
constexpr const char* KEY_V2   = "v2";    // current; includes bus_stops + 8-card model
constexpr const char* KEY_V1   = "v1";    // legacy; pre-bus-stops, 4-card model

// Mirror of the legacy v1 layout (pre-bus-stops, CARD_COUNT was 4).
// Kept here as the only producer of this shape — never added to public headers.
struct LegacyV1 {
    char     device_name[16];
    uint16_t live_timeout_s;
    uint16_t sleep_timeout_s;
    uint16_t dim_timeout_s;
    uint8_t  dim_level_pct;
    uint8_t  full_level_pct;
    uint32_t daily_token_cap;
    uint8_t  cards_enabled_mask;
    uint8_t  cards_order[4];
    uint8_t  cards_order_count;
    uint8_t  boot_card_id;
};
}  // namespace

Settings::Settings() {
    settings::setDefaults(data_, "Claude");
}

void Settings::begin(const char* default_name) {
    settings::setDefaults(data_, default_name);

    Preferences p;
    if (!p.begin(NS, /*readOnly=*/true)) return;

    // Try v2 first.
    settings::Settings stored = data_;
    size_t n = p.getBytesLength(KEY_V2);
    if (n == sizeof(settings::Settings)) {
        p.getBytes(KEY_V2, &stored, sizeof(stored));
        char err[64] = {};
        if (settings::validate(stored, err, sizeof(err))) {
            data_ = stored;
            p.end();
            return;
        }
    }

    // Fall back to v1: copy the overlapping fields, leave bus_stops zero,
    // and persist the upgraded blob so future boots take the v2 path.
    n = p.getBytesLength(KEY_V1);
    if (n == sizeof(LegacyV1)) {
        LegacyV1 v1{};
        p.getBytes(KEY_V1, &v1, sizeof(v1));
        // Map legacy fields into the new struct (which already has defaults
        // applied above, so bus_stops are zero-initialised).
        memcpy(data_.device_name, v1.device_name, sizeof(v1.device_name));
        data_.device_name[sizeof(v1.device_name) - 1] = '\0';
        data_.live_timeout_s    = v1.live_timeout_s;
        data_.sleep_timeout_s   = v1.sleep_timeout_s;
        data_.dim_timeout_s     = v1.dim_timeout_s;
        data_.dim_level_pct     = v1.dim_level_pct;
        data_.full_level_pct    = v1.full_level_pct;
        data_.daily_token_cap   = v1.daily_token_cap;
        data_.cards_enabled_mask = v1.cards_enabled_mask;
        for (uint8_t i = 0; i < 4; ++i) data_.cards_order[i] = v1.cards_order[i];
        for (uint8_t i = 4; i < settings::CARD_COUNT; ++i) data_.cards_order[i] = 0;
        data_.cards_order_count = v1.cards_order_count;
        data_.boot_card_id      = v1.boot_card_id;

        char err[64] = {};
        if (settings::validate(data_, err, sizeof(err))) {
            p.end();
            persist();   // writes the v2 blob
            return;
        }
        // Validation failed — fall through to defaults already in data_.
        settings::setDefaults(data_, default_name);
    }

    p.end();
}
```

And update `persist()` (around line 104) to write to `KEY_V2`:

```cpp
void Settings::persist() {
    Preferences p;
    if (!p.begin(NS, /*readOnly=*/false)) return;
    p.putBytes(KEY_V2, &data_, sizeof(data_));
    // Best-effort cleanup of the legacy blob; ignored if absent.
    p.remove(KEY_V1);
    p.end();
}
```

- [ ] **Step 4: Build to confirm it compiles**

Run: `pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -10`

Expected: build succeeds. (Linker errors for `BusCard`/`HttpServer` bus-stop endpoint are not yet in play because no consumer references them.)

- [ ] **Step 5: Commit**

```bash
git add src/core/Settings.h src/core/Settings.cpp
git commit -m "feat(settings): bump NVS key v1->v2 with one-shot legacy migration"
```

---

## Task 5: Create `lib/bus_arrivals` with `parseIso8601Delta` (TDD)

**Files:**
- Create: `lib/bus_arrivals/library.json`
- Create: `lib/bus_arrivals/bus_arrivals.h`
- Create: `lib/bus_arrivals/bus_arrivals.cpp`
- Create: `test/test_bus_arrivals/test_bus_arrivals.cpp`

- [ ] **Step 1: Create the PlatformIO library manifest**

Create `lib/bus_arrivals/library.json`:

```json
{
  "name": "bus_arrivals",
  "version": "0.1.0",
  "description": "Pure-C++ parser for api.busaunty.com bus arrival JSON.",
  "frameworks": "*",
  "platforms": "*",
  "dependencies": [
    {"name": "ArduinoJson", "version": "^7.0.0"}
  ]
}
```

- [ ] **Step 2: Write the public header**

Create `lib/bus_arrivals/bus_arrivals.h`:

```cpp
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace bus_arrivals {

constexpr uint8_t  kMaxServicesPerStop = 16;
constexpr uint8_t  kServiceNoLen       = 4;     // "961M\0" needs 5 bytes
constexpr size_t   kMaxResponseBytes   = 32 * 1024;

enum BusLoad : uint8_t { LOAD_UNKNOWN = 0, LOAD_SEA, LOAD_SDA, LOAD_LSD };
enum BusType : uint8_t { TYPE_UNKNOWN = 0, TYPE_SD,  TYPE_DD,  TYPE_BD  };

struct BusServiceArrival {
    char    service_no[kServiceNoLen + 1];   // up to "961M\0"
    int32_t eta_seconds_at_fetch;            // INT32_MIN sentinel = no data
    BusLoad load;
    BusType type;
    bool    wab;                             // parsed but not rendered v1
};

struct BusStopArrivals {
    BusServiceArrival services[kMaxServicesPerStop];
    uint8_t           service_count;
    uint32_t          fetched_at_ms;          // millis() at most recent successful parse
    uint32_t          last_fetch_success_ms;  // == fetched_at_ms; named for staleness checks
    bool              valid;                  // true once at least one fetch has succeeded
    char              last_error[32];         // "" iff most recent attempt succeeded
};

// Parse an ISO-8601 timestamp of the shape "YYYY-MM-DDTHH:MM:SS+HH:MM" (or
// with a fractional seconds component which is ignored) into seconds since
// the Unix epoch. Returns INT32_MIN on any malformed input.
int32_t parseIso8601Delta(const char* iso);

// Parse a complete BusArrival JSON response (shape from
// api.busaunty.com /api/v1/BusArrival?BusStopCode=NNNNN) into out.
// Returns true on success; on failure populates out.last_error and leaves
// out.valid as it was. The first stop in busStops[] is used; additional
// stops are ignored. Caller is responsible for setting out.fetched_at_ms.
bool parseBusArrivalsJson(const char* json, size_t json_len,
                          BusStopArrivals& out);

}  // namespace bus_arrivals
```

- [ ] **Step 3: Stub the implementation so the link succeeds**

Create `lib/bus_arrivals/bus_arrivals.cpp`:

```cpp
#include "bus_arrivals.h"

namespace bus_arrivals {

int32_t parseIso8601Delta(const char* /*iso*/) {
    return INT32_MIN;   // stub — Task 5 fills this in
}

bool parseBusArrivalsJson(const char* /*json*/, size_t /*json_len*/,
                          BusStopArrivals& /*out*/) {
    return false;       // stub — Task 6 fills this in
}

}  // namespace bus_arrivals
```

- [ ] **Step 4: Write the failing tests**

Create `test/test_bus_arrivals/test_bus_arrivals.cpp`:

```cpp
#include <unity.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "bus_arrivals.h"

void setUp(void) {}
void tearDown(void) {}

// Pre-computed expected unix timestamps. timegm is non-portable; instead we
// recompute via a deliberately simple unix-epoch helper inside each test
// where we need an authoritative answer.
static int32_t mkTime(int y, int mo, int d, int h, int mi, int s,
                      int off_h, int off_m) {
    // Days from 1970-01-01 to y-mo-d, treating mo as 1-based.
    static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    auto isLeap = [](int yy){
        return (yy % 4 == 0 && yy % 100 != 0) || (yy % 400 == 0);
    };
    int days = 0;
    for (int yr = 1970; yr < y; ++yr) days += isLeap(yr) ? 366 : 365;
    for (int m  = 1;    m  < mo; ++m)  {
        days += dim[m-1];
        if (m == 2 && isLeap(y)) days += 1;
    }
    days += (d - 1);
    int32_t secs = (int32_t)days * 86400 + h * 3600 + mi * 60 + s;
    int32_t off  = off_h * 3600 + (off_h < 0 ? -off_m * 60 : off_m * 60);
    return secs - off;
}

static void test_parses_singapore_offset(void) {
    int32_t got = bus_arrivals::parseIso8601Delta("2026-05-13T17:53:41+08:00");
    int32_t want = mkTime(2026, 5, 13, 17, 53, 41, 8, 0);
    TEST_ASSERT_EQUAL_INT32(want, got);
}

static void test_parses_with_fractional_seconds(void) {
    int32_t got = bus_arrivals::parseIso8601Delta("2026-05-13T17:47:51.46162+08:00");
    int32_t want = mkTime(2026, 5, 13, 17, 47, 51, 8, 0);
    TEST_ASSERT_EQUAL_INT32(want, got);
}

static void test_parses_zero_offset(void) {
    int32_t got = bus_arrivals::parseIso8601Delta("2026-05-13T09:53:41+00:00");
    int32_t want = mkTime(2026, 5, 13, 9, 53, 41, 0, 0);
    TEST_ASSERT_EQUAL_INT32(want, got);
}

static void test_parses_negative_offset(void) {
    int32_t got = bus_arrivals::parseIso8601Delta("2026-05-13T05:53:41-04:00");
    int32_t want = mkTime(2026, 5, 13, 5, 53, 41, -4, 0);
    TEST_ASSERT_EQUAL_INT32(want, got);
}

static void test_rejects_empty(void) {
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, bus_arrivals::parseIso8601Delta(""));
}

static void test_rejects_null(void) {
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, bus_arrivals::parseIso8601Delta(nullptr));
}

static void test_rejects_truncated(void) {
    TEST_ASSERT_EQUAL_INT32(INT32_MIN,
        bus_arrivals::parseIso8601Delta("2026-05-13T17:53"));
}

static void test_rejects_garbage(void) {
    TEST_ASSERT_EQUAL_INT32(INT32_MIN,
        bus_arrivals::parseIso8601Delta("not-a-date"));
}

static void test_rejects_missing_T(void) {
    TEST_ASSERT_EQUAL_INT32(INT32_MIN,
        bus_arrivals::parseIso8601Delta("2026-05-13 17:53:41+08:00"));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_parses_singapore_offset);
    RUN_TEST(test_parses_with_fractional_seconds);
    RUN_TEST(test_parses_zero_offset);
    RUN_TEST(test_parses_negative_offset);
    RUN_TEST(test_rejects_empty);
    RUN_TEST(test_rejects_null);
    RUN_TEST(test_rejects_truncated);
    RUN_TEST(test_rejects_garbage);
    RUN_TEST(test_rejects_missing_T);
    return UNITY_END();
}
```

- [ ] **Step 5: Run, expect failure**

Run: `pio test -e native -f test_bus_arrivals 2>&1 | tail -20`

Expected: every parser test fails (the stub returns `INT32_MIN` for everything).

- [ ] **Step 6: Implement `parseIso8601Delta`**

Replace `parseIso8601Delta` in `lib/bus_arrivals/bus_arrivals.cpp` with:

```cpp
#include <stdio.h>
#include <string.h>

namespace {

bool isLeap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

int32_t toUnix(int y, int mo, int d, int h, int mi, int s) {
    static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int days = 0;
    for (int yr = 1970; yr < y; ++yr) days += isLeap(yr) ? 366 : 365;
    for (int m  = 1;    m  < mo; ++m) {
        days += dim[m-1];
        if (m == 2 && isLeap(y)) days += 1;
    }
    days += (d - 1);
    return (int32_t)days * 86400 + h * 3600 + mi * 60 + s;
}

}  // namespace

namespace bus_arrivals {

int32_t parseIso8601Delta(const char* iso) {
    if (!iso) return INT32_MIN;
    // Minimum length: "YYYY-MM-DDTHH:MM:SS+HH:MM" = 25 chars.
    size_t n = strnlen(iso, 64);
    if (n < 25) return INT32_MIN;

    int y, mo, d, h, mi, s;
    char tsep, off_sign;
    int oh, om;
    // Use %2d / %4d width-bounded specifiers so trailing junk doesn't
    // poison the parse, and capture the T separator and timezone sign
    // so we can validate them explicitly.
    int matched = sscanf(iso, "%4d-%2d-%2dT%2d:%2d:%2d",
                         &y, &mo, &d, &h, &mi, &s);
    (void)tsep;
    if (matched != 6) return INT32_MIN;
    if (iso[10] != 'T') return INT32_MIN;

    // Skip an optional ".fff" fractional-seconds component.
    size_t off_pos = 19;
    if (off_pos < n && iso[off_pos] == '.') {
        off_pos++;
        while (off_pos < n && iso[off_pos] >= '0' && iso[off_pos] <= '9') {
            off_pos++;
        }
    }
    if (off_pos >= n) return INT32_MIN;
    off_sign = iso[off_pos];
    if (off_sign != '+' && off_sign != '-') return INT32_MIN;
    if (off_pos + 6 > n) return INT32_MIN;
    if (sscanf(iso + off_pos + 1, "%2d:%2d", &oh, &om) != 2) return INT32_MIN;

    if (mo < 1 || mo > 12 || d < 1 || d > 31) return INT32_MIN;
    if (h  < 0 || h  > 23) return INT32_MIN;
    if (mi < 0 || mi > 59) return INT32_MIN;
    if (s  < 0 || s  > 60) return INT32_MIN;
    if (oh < 0 || oh > 14) return INT32_MIN;
    if (om < 0 || om > 59) return INT32_MIN;

    int32_t local = toUnix(y, mo, d, h, mi, s);
    int32_t off   = oh * 3600 + om * 60;
    if (off_sign == '+') return local - off;
    return local + off;
}

bool parseBusArrivalsJson(const char* /*json*/, size_t /*json_len*/,
                          BusStopArrivals& /*out*/) {
    return false;       // stub — Task 6 fills this in
}

}  // namespace bus_arrivals
```

- [ ] **Step 7: Run, expect pass**

Run: `pio test -e native -f test_bus_arrivals 2>&1 | tail -20`

Expected: all 9 parser tests pass.

- [ ] **Step 8: Commit**

```bash
git add lib/bus_arrivals/ test/test_bus_arrivals/
git commit -m "feat(bus_arrivals): add ISO-8601 parser with overflow-safe input handling"
```

---

## Task 6: Implement `parseBusArrivalsJson` (TDD)

**Files:**
- Modify: `lib/bus_arrivals/bus_arrivals.cpp`
- Modify: `test/test_bus_arrivals/test_bus_arrivals.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `test/test_bus_arrivals/test_bus_arrivals.cpp` (before `int main`):

```cpp
static const char* kSampleJson = R"({
  "busStops": [{
    "BusStopCode": 50171,
    "Services": [
      {"ServiceNo": "21",  "NextBus":  {"EstimatedArrival": "2026-05-13T17:53:41+08:00", "Load": "SEA", "Feature": "WAB", "Type": "DD"}},
      {"ServiceNo": "129", "NextBus":  {"EstimatedArrival": "2026-05-13T17:50:48+08:00", "Load": "SEA", "Feature": "WAB", "Type": "DD"}},
      {"ServiceNo": "961M","NextBus":  {"EstimatedArrival": "2026-05-13T17:51:14+08:00", "Load": "SDA", "Feature": "",    "Type": "SD"}}
    ],
    "UpdatedAt": "2026-05-13T17:47:51.46162+08:00"
  }]
})";

static void test_parses_three_services(void) {
    bus_arrivals::BusStopArrivals out{};
    TEST_ASSERT_TRUE(bus_arrivals::parseBusArrivalsJson(
        kSampleJson, strlen(kSampleJson), out));
    TEST_ASSERT_EQUAL_UINT8(3, out.service_count);
    TEST_ASSERT_EQUAL_STRING("21",  out.services[0].service_no);
    TEST_ASSERT_EQUAL_STRING("129", out.services[1].service_no);
    TEST_ASSERT_EQUAL_STRING("961M", out.services[2].service_no);
    TEST_ASSERT_EQUAL_INT(bus_arrivals::LOAD_SEA, out.services[0].load);
    TEST_ASSERT_EQUAL_INT(bus_arrivals::LOAD_SDA, out.services[2].load);
    TEST_ASSERT_EQUAL_INT(bus_arrivals::TYPE_DD,  out.services[0].type);
    TEST_ASSERT_EQUAL_INT(bus_arrivals::TYPE_SD,  out.services[2].type);
    TEST_ASSERT_TRUE(out.valid);
    TEST_ASSERT_EQUAL_STRING("", out.last_error);
}

static void test_eta_relative_to_updatedat(void) {
    bus_arrivals::BusStopArrivals out{};
    TEST_ASSERT_TRUE(bus_arrivals::parseBusArrivalsJson(
        kSampleJson, strlen(kSampleJson), out));
    // 17:53:41 - 17:47:51 = 5m 50s = 350s. We accept 349..351 to absorb
    // the dropped fractional-seconds part of UpdatedAt (.46162 -> 0).
    TEST_ASSERT_INT_WITHIN(2, 350, out.services[0].eta_seconds_at_fetch);
}

static void test_truncates_at_max_services(void) {
    // Build a JSON with 25 services; expect first 16 to land.
    char buf[8192];
    int pos = snprintf(buf, sizeof(buf),
        "{\"busStops\":[{\"BusStopCode\":1,\"UpdatedAt\":"
        "\"2026-05-13T17:47:51+08:00\",\"Services\":[");
    for (int i = 0; i < 25; ++i) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "%s{\"ServiceNo\":\"%d\",\"NextBus\":{"
            "\"EstimatedArrival\":\"2026-05-13T17:50:00+08:00\","
            "\"Load\":\"SEA\",\"Type\":\"SD\"}}",
            i == 0 ? "" : ",", i);
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "]}]}");
    bus_arrivals::BusStopArrivals out{};
    TEST_ASSERT_TRUE(bus_arrivals::parseBusArrivalsJson(buf, (size_t)pos, out));
    TEST_ASSERT_EQUAL_UINT8(bus_arrivals::kMaxServicesPerStop,
                            out.service_count);
}

static void test_unknown_load_and_type_become_unknown(void) {
    const char* j = R"({"busStops":[{"BusStopCode":1,
      "UpdatedAt":"2026-05-13T17:47:51+08:00",
      "Services":[{"ServiceNo":"99","NextBus":{
        "EstimatedArrival":"2026-05-13T17:50:00+08:00",
        "Load":"???","Type":"??"}}]}]})";
    bus_arrivals::BusStopArrivals out{};
    TEST_ASSERT_TRUE(bus_arrivals::parseBusArrivalsJson(j, strlen(j), out));
    TEST_ASSERT_EQUAL_INT(bus_arrivals::LOAD_UNKNOWN, out.services[0].load);
    TEST_ASSERT_EQUAL_INT(bus_arrivals::TYPE_UNKNOWN, out.services[0].type);
}

static void test_missing_load_and_type_become_unknown(void) {
    const char* j = R"({"busStops":[{"BusStopCode":1,
      "UpdatedAt":"2026-05-13T17:47:51+08:00",
      "Services":[{"ServiceNo":"7","NextBus":{
        "EstimatedArrival":"2026-05-13T17:50:00+08:00"}}]}]})";
    bus_arrivals::BusStopArrivals out{};
    TEST_ASSERT_TRUE(bus_arrivals::parseBusArrivalsJson(j, strlen(j), out));
    TEST_ASSERT_EQUAL_INT(bus_arrivals::LOAD_UNKNOWN, out.services[0].load);
    TEST_ASSERT_EQUAL_INT(bus_arrivals::TYPE_UNKNOWN, out.services[0].type);
}

static void test_malformed_eta_leaves_sentinel(void) {
    const char* j = R"({"busStops":[{"BusStopCode":1,
      "UpdatedAt":"2026-05-13T17:47:51+08:00",
      "Services":[{"ServiceNo":"7","NextBus":{
        "EstimatedArrival":"not-a-date","Load":"SEA","Type":"SD"}}]}]})";
    bus_arrivals::BusStopArrivals out{};
    TEST_ASSERT_TRUE(bus_arrivals::parseBusArrivalsJson(j, strlen(j), out));
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, out.services[0].eta_seconds_at_fetch);
}

static void test_empty_busStops_yields_zero_count(void) {
    const char* j = R"({"busStops":[]})";
    bus_arrivals::BusStopArrivals out{};
    TEST_ASSERT_TRUE(bus_arrivals::parseBusArrivalsJson(j, strlen(j), out));
    TEST_ASSERT_EQUAL_UINT8(0, out.service_count);
    TEST_ASSERT_TRUE(out.valid);
}

static void test_truncated_json_returns_error(void) {
    const char* j = R"({"busStops":[{"BusStopCode":1,"Services":[)";
    bus_arrivals::BusStopArrivals out{};
    out.valid = false;
    TEST_ASSERT_FALSE(bus_arrivals::parseBusArrivalsJson(j, strlen(j), out));
    TEST_ASSERT_FALSE(out.valid);
    TEST_ASSERT_NOT_EQUAL(0, out.last_error[0]);
}

static void test_response_too_large_rejected(void) {
    char* big = (char*)calloc(1, bus_arrivals::kMaxResponseBytes + 16);
    TEST_ASSERT_NOT_NULL(big);
    memset(big, 'x', bus_arrivals::kMaxResponseBytes + 8);
    bus_arrivals::BusStopArrivals out{};
    TEST_ASSERT_FALSE(bus_arrivals::parseBusArrivalsJson(
        big, bus_arrivals::kMaxResponseBytes + 8, out));
    TEST_ASSERT_TRUE(strstr(out.last_error, "too large") != nullptr);
    free(big);
}

static void test_service_no_truncated_safely(void) {
    // ServiceNo of "TOOLONG" should be stored as "TOOL" + null.
    const char* j = R"({"busStops":[{"BusStopCode":1,
      "UpdatedAt":"2026-05-13T17:47:51+08:00",
      "Services":[{"ServiceNo":"TOOLONG","NextBus":{
        "EstimatedArrival":"2026-05-13T17:50:00+08:00",
        "Load":"SEA","Type":"SD"}}]}]})";
    bus_arrivals::BusStopArrivals out{};
    TEST_ASSERT_TRUE(bus_arrivals::parseBusArrivalsJson(j, strlen(j), out));
    TEST_ASSERT_EQUAL_size_t(bus_arrivals::kServiceNoLen,
                             strlen(out.services[0].service_no));
}
```

Register them in `main()`:

```cpp
    RUN_TEST(test_parses_three_services);
    RUN_TEST(test_eta_relative_to_updatedat);
    RUN_TEST(test_truncates_at_max_services);
    RUN_TEST(test_unknown_load_and_type_become_unknown);
    RUN_TEST(test_missing_load_and_type_become_unknown);
    RUN_TEST(test_malformed_eta_leaves_sentinel);
    RUN_TEST(test_empty_busStops_yields_zero_count);
    RUN_TEST(test_truncated_json_returns_error);
    RUN_TEST(test_response_too_large_rejected);
    RUN_TEST(test_service_no_truncated_safely);
```

- [ ] **Step 2: Run, expect failure**

Run: `pio test -e native -f test_bus_arrivals 2>&1 | tail -20`

Expected: every JSON test fails (stub returns false).

- [ ] **Step 3: Implement `parseBusArrivalsJson`**

Replace the stub `parseBusArrivalsJson` in `lib/bus_arrivals/bus_arrivals.cpp` with:

```cpp
#include <ArduinoJson.h>

namespace {

void setError(bus_arrivals::BusStopArrivals& out, const char* msg) {
    size_t cap = sizeof(out.last_error) - 1;
    size_t n = strlen(msg);
    if (n > cap) n = cap;
    memcpy(out.last_error, msg, n);
    out.last_error[n] = '\0';
}

bus_arrivals::BusLoad parseLoad(const char* s) {
    if (!s) return bus_arrivals::LOAD_UNKNOWN;
    if (strcmp(s, "SEA") == 0) return bus_arrivals::LOAD_SEA;
    if (strcmp(s, "SDA") == 0) return bus_arrivals::LOAD_SDA;
    if (strcmp(s, "LSD") == 0) return bus_arrivals::LOAD_LSD;
    return bus_arrivals::LOAD_UNKNOWN;
}

bus_arrivals::BusType parseType(const char* s) {
    if (!s) return bus_arrivals::TYPE_UNKNOWN;
    if (strcmp(s, "SD") == 0) return bus_arrivals::TYPE_SD;
    if (strcmp(s, "DD") == 0) return bus_arrivals::TYPE_DD;
    if (strcmp(s, "BD") == 0) return bus_arrivals::TYPE_BD;
    return bus_arrivals::TYPE_UNKNOWN;
}

}  // namespace

namespace bus_arrivals {

bool parseBusArrivalsJson(const char* json, size_t json_len,
                          BusStopArrivals& out) {
    out.last_error[0] = '\0';
    if (json_len > kMaxResponseBytes) {
        setError(out, "response too large");
        return false;
    }
    if (!json || json_len == 0) {
        setError(out, "empty response");
        return false;
    }

    // Filter: only materialize the fields we render. This bounds parse
    // memory regardless of how many NextBus2/NextBus3/Feature/UpdatedAt
    // entries the server sends.
    JsonDocument filter;
    filter["busStops"][0]["BusStopCode"]                        = true;
    filter["busStops"][0]["UpdatedAt"]                          = true;
    filter["busStops"][0]["Services"][0]["ServiceNo"]           = true;
    filter["busStops"][0]["Services"][0]["NextBus"]["EstimatedArrival"] = true;
    filter["busStops"][0]["Services"][0]["NextBus"]["Load"]     = true;
    filter["busStops"][0]["Services"][0]["NextBus"]["Type"]     = true;
    filter["busStops"][0]["Services"][0]["NextBus"]["Feature"]  = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(
        doc, json, json_len, DeserializationOption::Filter(filter));
    if (err == DeserializationError::NoMemory) {
        setError(out, "json too big");
        return false;
    }
    if (err) {
        setError(out, err.c_str());
        return false;
    }

    JsonArrayConst stops = doc["busStops"].as<JsonArrayConst>();
    if (stops.size() == 0) {
        out.service_count = 0;
        out.valid = true;
        return true;
    }
    JsonObjectConst stop = stops[0].as<JsonObjectConst>();

    int32_t updated_unix = parseIso8601Delta(stop["UpdatedAt"] | "");
    JsonArrayConst svcs = stop["Services"].as<JsonArrayConst>();

    out.service_count = 0;
    for (JsonObjectConst svc : svcs) {
        if (out.service_count >= kMaxServicesPerStop) break;
        BusServiceArrival& dst = out.services[out.service_count++];

        const char* sn = svc["ServiceNo"] | "";
        size_t snn = strnlen(sn, kServiceNoLen + 8);
        if (snn > kServiceNoLen) snn = kServiceNoLen;
        memcpy(dst.service_no, sn, snn);
        dst.service_no[snn] = '\0';

        JsonObjectConst nb = svc["NextBus"].as<JsonObjectConst>();
        const char* iso = nb["EstimatedArrival"] | "";
        int32_t arr = parseIso8601Delta(iso);
        if (arr == INT32_MIN || updated_unix == INT32_MIN) {
            dst.eta_seconds_at_fetch = INT32_MIN;
        } else {
            dst.eta_seconds_at_fetch = arr - updated_unix;
        }
        dst.load = parseLoad(nb["Load"] | (const char*)nullptr);
        dst.type = parseType(nb["Type"] | (const char*)nullptr);
        const char* feat = nb["Feature"] | "";
        dst.wab = (strcmp(feat, "WAB") == 0);
    }

    out.valid = true;
    return true;
}

}  // namespace bus_arrivals
```

- [ ] **Step 4: Run, expect pass**

Run: `pio test -e native -f test_bus_arrivals 2>&1 | tail -20`

Expected: all 19 bus_arrivals tests pass.

- [ ] **Step 5: Commit**

```bash
git add lib/bus_arrivals/ test/test_bus_arrivals/
git commit -m "feat(bus_arrivals): bounded JSON parse with ArduinoJson Filter"
```

---

## Task 7: Add `onShow`/`onHide` virtuals to `Card` and wire them in `CardStack`

**Files:**
- Modify: `src/ui/Card.h`
- Modify: `src/ui/CardStack.cpp`

- [ ] **Step 1: Add the virtuals**

In `src/ui/Card.h`, after the `handleButton` default override (line 29), add:

```cpp
    // Lifecycle hooks. CardStack invokes onShow() when this card becomes
    // the active one (push, pop revealing this card, setIndex, or
    // pushOverlay onto this card), and onHide() on the reverse transition.
    // Default no-op; cards that need to start/stop work on visibility
    // (e.g. periodic network fetches) override these.
    virtual void onShow() {}
    virtual void onHide() {}
```

- [ ] **Step 2: Invoke them from `CardStack`**

In `src/ui/CardStack.cpp`, replace the existing implementations of `setIndex`, `pushOverlay`, `clearOverlay`, `next`, and `prev` with versions that emit `onHide` on the prior active and `onShow` on the new active. The pattern is:

```cpp
void CardStack::setIndex(size_t i) {
    if (overlay_ || cards_.empty()) return;
    if (i >= cards_.size()) i = cards_.size() - 1;
    Card* prev_a = active();
    index_ = i;
    Card* a = active();
    if (prev_a && prev_a != a) prev_a->onHide();
    if (a) {
        if (a != prev_a) a->onShow();
        a->invalidate();
    }
}

void CardStack::pushOverlay(Card* card) {
    if (overlay_ == card) return;
    Card* prev_a = active();
    overlay_ = card;
    Card* a = active();
    if (prev_a && prev_a != a) prev_a->onHide();
    if (overlay_) {
        if (overlay_ != prev_a) overlay_->onShow();
        overlay_->invalidate();
    }
}

void CardStack::clearOverlay() {
    if (!overlay_) return;
    Card* prev_a = active();   // == overlay_
    overlay_ = nullptr;
    Card* a = active();
    if (prev_a) prev_a->onHide();
    if (a) {
        a->onShow();
        a->invalidate();
    }
}

void CardStack::next() {
    if (overlay_ || cards_.empty()) return;
    Card* prev_a = active();
    index_ = (index_ + 1) % cards_.size();
    Card* a = active();
    if (prev_a && prev_a != a) prev_a->onHide();
    if (a) {
        if (a != prev_a) a->onShow();
        a->invalidate();
    }
}

void CardStack::prev() {
    if (overlay_ || cards_.empty()) return;
    Card* prev_a = active();
    index_ = (index_ + cards_.size() - 1) % cards_.size();
    Card* a = active();
    if (prev_a && prev_a != a) prev_a->onHide();
    if (a) {
        if (a != prev_a) a->onShow();
        a->invalidate();
    }
}
```

`addCard` and `clear` remain unchanged. Reasoning: the *first* card added isn't yet "active" until something queries `active()` for rendering — but `CardController::rebuildStack()` calls `setIndex` immediately after, which triggers `onShow` for the boot card. Verify this in Task 14.

- [ ] **Step 3: Build to confirm everything still compiles**

Run: `pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -10`

Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/ui/Card.h src/ui/CardStack.cpp
git commit -m "feat(ui): add Card::onShow/onHide hooks invoked by CardStack transitions"
```

---

## Task 8: `BusArrivalsFetcher` — Arduino-only HTTPClient wrapper

**Files:**
- Create: `src/net/BusArrivalsFetcher.h`
- Create: `src/net/BusArrivalsFetcher.cpp`

No native test possible (HTTPClient isn't available off-target). Verified by build + on-device manual checklist (Task 26).

- [ ] **Step 1: Create the header**

Create `src/net/BusArrivalsFetcher.h`:

```cpp
#pragma once

#include <stdint.h>

#include "bus_arrivals.h"

namespace net {

// Synchronous fetch of one bus stop's arrivals from api.busaunty.com.
// HTTPS via NetworkClientSecure with setInsecure() — the data is public
// and unauthenticated, so a CA bundle is intentionally not used.
//
// On success: out.valid = true, out.service_count populated, out.fetched_at_ms
// and out.last_fetch_success_ms set to now_ms, out.last_error = "".
// On failure: out.last_error populated; out.valid is left as it was so callers
// can detect "stale data with prior values" vs "never had data".
//
// Bounds: response body capped at bus_arrivals::kMaxResponseBytes; HTTP
// timeout 8 s; service count capped at bus_arrivals::kMaxServicesPerStop.
class BusArrivalsFetcher {
public:
    // code is the 5-digit Singapore stop code, null-terminated.
    bool fetch(const char* code, uint32_t now_ms,
               bus_arrivals::BusStopArrivals& out);
};

}  // namespace net
```

- [ ] **Step 2: Create the implementation**

Create `src/net/BusArrivalsFetcher.cpp`:

```cpp
#include "BusArrivalsFetcher.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <stdio.h>
#include <string.h>

namespace net {

namespace {
constexpr uint32_t kHttpTimeoutMs = 8000;

void setErr(bus_arrivals::BusStopArrivals& out, const char* msg) {
    size_t cap = sizeof(out.last_error) - 1;
    size_t n = strlen(msg);
    if (n > cap) n = cap;
    memcpy(out.last_error, msg, n);
    out.last_error[n] = '\0';
}
}  // namespace

bool BusArrivalsFetcher::fetch(const char* code, uint32_t now_ms,
                               bus_arrivals::BusStopArrivals& out) {
    if (!code || code[0] == '\0') {
        setErr(out, "no stop code");
        return false;
    }

    NetworkClientSecure client;
    client.setInsecure();   // public, unauthenticated data

    char url[96];
    snprintf(url, sizeof(url),
             "https://api.busaunty.com/api/v1/BusArrival?BusStopCode=%s",
             code);

    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    if (!http.begin(client, url)) {
        setErr(out, "http begin failed");
        return false;
    }
    http.addHeader("User-Agent", "claude-buddy/1");
    http.addHeader("Accept", "application/json");

    int status = http.GET();
    if (status != 200) {
        char msg[32];
        snprintf(msg, sizeof(msg), "http %d", status);
        setErr(out, msg);
        http.end();
        return false;
    }

    int body_len = http.getSize();   // -1 if chunked
    if (body_len > (int)bus_arrivals::kMaxResponseBytes) {
        setErr(out, "response too large");
        http.end();
        return false;
    }

    // Read into a heap buffer with a hard cap. We can't stream-parse and
    // also enforce the size cap with the chunked path cleanly, so we
    // accumulate into a bounded buffer.
    size_t cap = (body_len > 0)
        ? (size_t)body_len + 1
        : bus_arrivals::kMaxResponseBytes + 1;
    if (cap > bus_arrivals::kMaxResponseBytes + 1) {
        cap = bus_arrivals::kMaxResponseBytes + 1;
    }
    char* buf = (char*)malloc(cap);
    if (!buf) {
        setErr(out, "oom");
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t pos = 0;
    uint32_t deadline = millis() + kHttpTimeoutMs;
    while (http.connected() && pos + 1 < cap && (int32_t)(deadline - millis()) > 0) {
        size_t avail = stream->available();
        if (avail > 0) {
            size_t want = avail;
            if (want > cap - 1 - pos) want = cap - 1 - pos;
            int got = stream->readBytes(buf + pos, want);
            if (got <= 0) break;
            pos += (size_t)got;
            if (pos >= bus_arrivals::kMaxResponseBytes) {
                setErr(out, "response too large");
                free(buf);
                http.end();
                return false;
            }
        } else {
            delay(2);
        }
        if (body_len > 0 && (int)pos >= body_len) break;
    }
    buf[pos] = '\0';
    http.end();

    if (pos == 0) {
        setErr(out, "empty body");
        free(buf);
        return false;
    }

    bool ok = bus_arrivals::parseBusArrivalsJson(buf, pos, out);
    free(buf);
    if (!ok) return false;

    out.fetched_at_ms          = now_ms;
    out.last_fetch_success_ms  = now_ms;
    return true;
}

}  // namespace net
```

- [ ] **Step 3: Build to confirm**

Run: `pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -10`

Expected: build succeeds. There are still no consumers of `BusArrivalsFetcher`, so the linker may garbage-collect it; that's fine.

- [ ] **Step 4: Commit**

```bash
git add src/net/BusArrivalsFetcher.h src/net/BusArrivalsFetcher.cpp
git commit -m "feat(net): add BusArrivalsFetcher with bounded body read and setInsecure TLS"
```

---

## Task 9: `BusCard` skeleton (header + lifecycle stubs, no render yet)

**Files:**
- Create: `src/ui/cards/BusCard.h`
- Create: `src/ui/cards/BusCard.cpp`

- [ ] **Step 1: Create the header**

Create `src/ui/cards/BusCard.h`:

```cpp
#pragma once

#include <stdint.h>

#include "../Card.h"
#include "../../core/Settings.h"
#include "../../net/BusArrivalsFetcher.h"
#include "../../net/WifiManager.h"
#include "bus_arrivals.h"
#include "settings_model.h"

class BusCard : public Card {
public:
    BusCard(uint8_t slot_index,
            const Settings& settings,
            const WifiManager& wifi);

    void invalidate() override;
    bool isDirty() const override;
    void render(Display& display) override;
    void tick(uint32_t now_ms) override;
    bool handleButton(ButtonEvent ev, uint32_t now_ms) override;
    void onShow() override;
    void onHide() override;

    static constexpr uint8_t  kViewportRows  = 7;
    static constexpr uint32_t kFetchPeriodMs = 30000;
    static constexpr uint32_t kStaleAfterMs  = 90000;
    static constexpr uint32_t kHintShowMs    = 3000;

private:
    enum class DisplayState : uint8_t {
        Loading,
        NoWifi,
        Stale,
        Empty,
        BadCode,
        FetchError,
        Normal,
    };

    DisplayState computeState(uint32_t now_ms) const;
    bool         shouldFetch(uint32_t now_ms) const;
    void         doFetch(uint32_t now_ms);
    int          displayedMinute(uint32_t now_ms,
                                 const bus_arrivals::BusServiceArrival& svc) const;

    void renderHeader(class Adafruit_ST7789& tft, DisplayState state);
    void renderRows(class Adafruit_ST7789& tft, uint32_t now_ms);
    void renderCenterMessage(class Adafruit_ST7789& tft,
                             const char* line1, const char* line2);
    void renderScrollHint(class Adafruit_ST7789& tft, uint32_t now_ms);
    void clearBody(class Adafruit_ST7789& tft);

    uint8_t                          slot_;
    const Settings&                  settings_;
    const WifiManager&               wifi_;
    net::BusArrivalsFetcher          fetcher_;
    bus_arrivals::BusStopArrivals    data_;
    uint32_t                         last_fetch_attempt_ms_;
    uint32_t                         shown_at_ms_;
    uint32_t                         last_tick_minute_;
    uint8_t                          first_visible_;
    bool                             ever_drawn_;
    bool                             visible_;
    bool                             dirty_;

    // Snapshots for diff-based row repaint.
    bus_arrivals::BusServiceArrival  last_drawn_[kViewportRows];
    int                              last_drawn_minute_[kViewportRows];
    uint8_t                          last_drawn_first_visible_;
    char                             last_drawn_label_[settings::MAX_BUS_LABEL_LEN + 1];
    char                             last_drawn_code_[settings::BUS_STOP_CODE_LEN + 1];
    bool                             last_drawn_wifi_up_;
    DisplayState                     last_drawn_state_;
};
```

- [ ] **Step 2: Create the implementation skeleton**

Create `src/ui/cards/BusCard.cpp`:

```cpp
#include "BusCard.h"

#include <Arduino.h>
#include <string.h>

#include "../../display/Display.h"

BusCard::BusCard(uint8_t slot_index,
                 const Settings& settings,
                 const WifiManager& wifi)
    : slot_(slot_index),
      settings_(settings),
      wifi_(wifi),
      data_{},
      last_fetch_attempt_ms_(0),
      shown_at_ms_(0),
      last_tick_minute_(0),
      first_visible_(0),
      ever_drawn_(false),
      visible_(false),
      dirty_(true),
      last_drawn_first_visible_(0),
      last_drawn_label_{0},
      last_drawn_code_{0},
      last_drawn_wifi_up_(false),
      last_drawn_state_(DisplayState::Loading) {
    for (uint8_t i = 0; i < kViewportRows; ++i) {
        last_drawn_[i] = {};
        last_drawn_minute_[i] = INT32_MIN;
    }
}

void BusCard::invalidate() {
    ever_drawn_ = false;
    dirty_      = true;
    last_drawn_first_visible_ = 0xFF;
    last_drawn_state_         = DisplayState::Loading;
    for (uint8_t i = 0; i < kViewportRows; ++i) {
        last_drawn_[i] = {};
        last_drawn_minute_[i] = INT32_MIN;
    }
    last_drawn_label_[0] = '\xFF';
    last_drawn_code_[0]  = '\xFF';
    last_drawn_wifi_up_  = !wifi_.isConnected();   // force flip
}

bool BusCard::isDirty() const {
    return dirty_ || !ever_drawn_;
}

void BusCard::onShow() {
    visible_ = true;
    shown_at_ms_ = millis();
    // Trigger an immediate fetch on the next tick.
    last_fetch_attempt_ms_ = 0;
    dirty_ = true;
}

void BusCard::onHide() {
    visible_ = false;
}

void BusCard::tick(uint32_t now_ms) {
    if (!visible_) return;

    if (!wifi_.isConnected()) {
        if (last_drawn_wifi_up_) dirty_ = true;
        return;
    }

    if (shouldFetch(now_ms)) {
        doFetch(now_ms);
        dirty_ = true;
        return;
    }

    if (data_.valid) {
        uint32_t minute = (now_ms - data_.fetched_at_ms) / 60000;
        if (minute != last_tick_minute_) {
            last_tick_minute_ = minute;
            dirty_ = true;
        }
    }
}

bool BusCard::shouldFetch(uint32_t now_ms) const {
    if (last_fetch_attempt_ms_ == 0) return true;
    return (now_ms - last_fetch_attempt_ms_) >= kFetchPeriodMs;
}

void BusCard::doFetch(uint32_t now_ms) {
    last_fetch_attempt_ms_ = now_ms;
    const char* code = settings_.data().bus_stops[slot_].code;
    if (code[0] == '\0') {
        // Slot got cleared while card was in stack; rebuildStack will
        // remove us shortly. Nothing to do.
        return;
    }
    fetcher_.fetch(code, now_ms, data_);
    last_tick_minute_ = 0;
}

bool BusCard::handleButton(ButtonEvent ev, uint32_t now_ms) {
    (void)now_ms;
    if (ev == BTN_CENTER && data_.service_count > kViewportRows) {
        first_visible_ = (uint8_t)((first_visible_ + 1) % data_.service_count);
        dirty_ = true;
        return true;
    }
    return false;
}

int BusCard::displayedMinute(uint32_t now_ms,
                             const bus_arrivals::BusServiceArrival& svc) const {
    if (svc.eta_seconds_at_fetch == INT32_MIN) return INT32_MIN;
    int32_t elapsed = (int32_t)((now_ms - data_.fetched_at_ms) / 1000);
    int32_t remaining = svc.eta_seconds_at_fetch - elapsed;
    if (remaining <= 0) return 0;
    return (int)(remaining / 60);
}

BusCard::DisplayState BusCard::computeState(uint32_t now_ms) const {
    if (!wifi_.isConnected()) return DisplayState::NoWifi;
    if (!data_.valid) {
        if (data_.last_error[0] == '\0') return DisplayState::Loading;
        return DisplayState::FetchError;
    }
    // valid == true.
    if (data_.last_error[0] != '\0') {
        if ((now_ms - data_.last_fetch_success_ms) > kStaleAfterMs) {
            return DisplayState::Stale;
        }
    }
    if (data_.service_count == 0) return DisplayState::Empty;
    return DisplayState::Normal;
}

void BusCard::render(Display& /*display*/) {
    // Implemented in Task 10 (states + header) and Task 11 (rows).
    dirty_      = false;
    ever_drawn_ = true;
}
```

- [ ] **Step 3: Build to confirm it links (no consumers yet)**

Run: `pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -10`

Expected: build succeeds. The card is unreferenced — that's fine for now.

- [ ] **Step 4: Commit**

```bash
git add src/ui/cards/BusCard.h src/ui/cards/BusCard.cpp
git commit -m "feat(ui): BusCard skeleton with lifecycle, tick, and state computation"
```

---

## Task 10: `BusCard` render — header + state overlays

**Files:**
- Modify: `src/ui/cards/BusCard.cpp`

- [ ] **Step 1: Add layout constants and small helpers near the top**

In `src/ui/cards/BusCard.cpp`, replace the leading anonymous-block-less section with:

```cpp
#include "BusCard.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "../../display/Display.h"

namespace {

constexpr int kHeaderY     = 0;
constexpr int kHeaderH     = 14;
constexpr int kDividerY    = 14;
constexpr int kBodyTopY    = 18;
constexpr int kRowH        = 16;
constexpr int kBodyW       = 240;
constexpr int kBodyH       = 135 - kBodyTopY;

constexpr int kCol_Service = 8;
constexpr int kCol_Dot     = 50;
constexpr int kCol_Load    = 64;
constexpr int kCol_Eta     = 110;
constexpr int kEtaW        = 80;
constexpr int kCol_Type    = 200;

constexpr int kScrollX     = 234;
constexpr int kScrollW     = 4;
constexpr int kScrollY     = kBodyTopY + 2;
constexpr int kScrollH     = kBodyH - 4;

constexpr uint16_t kColBg       = ST77XX_BLACK;
constexpr uint16_t kColFg       = ST77XX_WHITE;
constexpr uint16_t kColDim      = 0x7BEF;
constexpr uint16_t kColDivider  = 0x39E7;
constexpr uint16_t kColLoadSEA  = ST77XX_GREEN;
constexpr uint16_t kColLoadSDA  = 0xFD20;            // amber
constexpr uint16_t kColLoadLSD  = ST77XX_RED;
constexpr uint16_t kColEtaArr   = ST77XX_YELLOW;
constexpr uint16_t kColType_DD  = ST77XX_CYAN;
constexpr uint16_t kColType_SD  = 0xC618;            // light grey
constexpr uint16_t kColType_BD  = ST77XX_MAGENTA;
constexpr uint16_t kColWarn     = 0xFD20;

void formatHeaderLabel(const settings::BusStopSlot& slot,
                       char* out, size_t out_len) {
    if (slot.label[0] != '\0') {
        snprintf(out, out_len, "%s", slot.label);
    } else {
        snprintf(out, out_len, "Stop %s", slot.code);
    }
}

uint16_t loadColor(bus_arrivals::BusLoad l) {
    switch (l) {
        case bus_arrivals::LOAD_SEA: return kColLoadSEA;
        case bus_arrivals::LOAD_SDA: return kColLoadSDA;
        case bus_arrivals::LOAD_LSD: return kColLoadLSD;
        default:                     return kColDim;
    }
}

const char* loadLabel(bus_arrivals::BusLoad l) {
    switch (l) {
        case bus_arrivals::LOAD_SEA: return "SEA";
        case bus_arrivals::LOAD_SDA: return "SDA";
        case bus_arrivals::LOAD_LSD: return "LSD";
        default:                     return "---";
    }
}

uint16_t typeColor(bus_arrivals::BusType t) {
    switch (t) {
        case bus_arrivals::TYPE_DD: return kColType_DD;
        case bus_arrivals::TYPE_SD: return kColType_SD;
        case bus_arrivals::TYPE_BD: return kColType_BD;
        default:                    return kColDim;
    }
}

const char* typeLabel(bus_arrivals::BusType t) {
    switch (t) {
        case bus_arrivals::TYPE_DD: return "[DD]";
        case bus_arrivals::TYPE_SD: return "[SD]";
        case bus_arrivals::TYPE_BD: return "[BD]";
        default:                    return "[--]";
    }
}

}  // namespace
```

- [ ] **Step 2: Implement the header and centre-message renderers**

Add these member implementations to `src/ui/cards/BusCard.cpp`:

```cpp
void BusCard::renderHeader(Adafruit_ST7789& tft, DisplayState state) {
    char label[32];
    formatHeaderLabel(settings_.data().bus_stops[slot_], label, sizeof(label));

    tft.fillRect(0, kHeaderY, 240, kHeaderH, kColBg);
    tft.setTextColor(kColFg, kColBg);
    tft.setTextSize(1);
    tft.setCursor(8, 4);
    tft.print(label);

    if (state == DisplayState::Stale) {
        // Draw a warning glyph on the right side.
        tft.setTextColor(kColWarn, kColBg);
        tft.setCursor(220, 4);
        tft.print("!");
    }

    tft.drawFastHLine(0, kDividerY, 240, kColDivider);
}

void BusCard::clearBody(Adafruit_ST7789& tft) {
    tft.fillRect(0, kBodyTopY, kBodyW, kBodyH, kColBg);
}

void BusCard::renderCenterMessage(Adafruit_ST7789& tft,
                                  const char* line1, const char* line2) {
    clearBody(tft);
    tft.setTextSize(2);
    tft.setTextColor(kColFg, kColBg);
    int16_t x1, y1; uint16_t w1, h1;
    tft.getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    int x = (240 - (int)w1) / 2;
    int y = kBodyTopY + (kBodyH / 2) - (int)h1 - 2;
    tft.setCursor(x, y);
    tft.print(line1);
    if (line2 && line2[0]) {
        tft.setTextSize(1);
        tft.setTextColor(kColDim, kColBg);
        int16_t x2, y2; uint16_t w2, h2;
        tft.getTextBounds(line2, 0, 0, &x2, &y2, &w2, &h2);
        int x_l2 = (240 - (int)w2) / 2;
        tft.setCursor(x_l2, y + (int)h1 + 6);
        tft.print(line2);
    }
}
```

- [ ] **Step 3: Implement the state-router `render()`**

Replace the existing `render()` body in `src/ui/cards/BusCard.cpp` with:

```cpp
void BusCard::render(Display& display) {
    Adafruit_ST7789& tft = display.tft();
    uint32_t now_ms = millis();

    DisplayState state = computeState(now_ms);

    char curr_label[32];
    formatHeaderLabel(settings_.data().bus_stops[slot_], curr_label, sizeof(curr_label));

    bool full_clear = !ever_drawn_
        || state != last_drawn_state_
        || strncmp(curr_label, last_drawn_label_, sizeof(last_drawn_label_)) != 0
        || strncmp(settings_.data().bus_stops[slot_].code,
                   last_drawn_code_, sizeof(last_drawn_code_)) != 0
        || wifi_.isConnected() != last_drawn_wifi_up_;

    if (full_clear) {
        tft.fillScreen(kColBg);
    }

    renderHeader(tft, state);

    char codebuf[16];
    snprintf(codebuf, sizeof(codebuf), "%s",
             settings_.data().bus_stops[slot_].code);

    switch (state) {
        case DisplayState::Loading: {
            char ip_line[32];
            snprintf(ip_line, sizeof(ip_line), "Wi-Fi %s",
                     wifi_.ip().toString().c_str());
            renderCenterMessage(tft, "Loading...", ip_line);
            break;
        }
        case DisplayState::NoWifi:
            renderCenterMessage(tft, "No Wi-Fi", "Configure at claude.local");
            break;
        case DisplayState::Empty: {
            char l[32];
            snprintf(l, sizeof(l), "for stop %s", codebuf);
            renderCenterMessage(tft, "No services found", l);
            break;
        }
        case DisplayState::BadCode: {
            char l[40];
            snprintf(l, sizeof(l), "Stop %s unknown", codebuf);
            renderCenterMessage(tft, l, "check the web UI");
            break;
        }
        case DisplayState::FetchError:
            renderCenterMessage(tft, "Bus times unavailable",
                                data_.last_error);
            break;
        case DisplayState::Normal:
        case DisplayState::Stale:
            renderRows(tft, now_ms);
            renderScrollHint(tft, now_ms);
            break;
    }

    // Snapshot for next-render diff.
    strncpy(last_drawn_label_, curr_label, sizeof(last_drawn_label_) - 1);
    last_drawn_label_[sizeof(last_drawn_label_) - 1] = '\0';
    strncpy(last_drawn_code_, codebuf, sizeof(last_drawn_code_) - 1);
    last_drawn_code_[sizeof(last_drawn_code_) - 1] = '\0';
    last_drawn_wifi_up_ = wifi_.isConnected();
    last_drawn_state_   = state;

    dirty_      = false;
    ever_drawn_ = true;
}

// Stubs for the row + scroll renderers; filled in by Task 11.
void BusCard::renderRows(Adafruit_ST7789& tft, uint32_t /*now_ms*/) {
    clearBody(tft);
    tft.setTextColor(kColDim, kColBg);
    tft.setTextSize(1);
    tft.setCursor(8, kBodyTopY + 4);
    tft.print("(rows render in Task 11)");
}

void BusCard::renderScrollHint(Adafruit_ST7789& /*tft*/,
                               uint32_t /*now_ms*/) {
    // Implemented in Task 11.
}
```

Note: the constructor's initial `last_drawn_label_/_code_` fill of `'\xFF'` from Task 9 ensures the first `render()` after `invalidate()` triggers `full_clear`. Add `#include <stdio.h>` at the top of the file if not already there.

- [ ] **Step 4: Build to confirm**

Run: `pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -10`

Expected: build succeeds.

- [ ] **Step 5: Commit**

```bash
git add src/ui/cards/BusCard.cpp
git commit -m "feat(ui): BusCard header and centre-message rendering for non-normal states"
```

---

## Task 11: `BusCard` render — service rows + scroll bar + hint

**Files:**
- Modify: `src/ui/cards/BusCard.cpp`

- [ ] **Step 1: Implement `renderRows`**

Replace the stub `renderRows` in `src/ui/cards/BusCard.cpp` with:

```cpp
void BusCard::renderRows(Adafruit_ST7789& tft, uint32_t now_ms) {
    const bool stale = (data_.last_error[0] != '\0');
    const uint16_t fg = stale ? kColDim : kColFg;

    uint8_t total = data_.service_count;
    if (total == 0) { clearBody(tft); return; }

    bool full_rows_repaint =
        first_visible_ != last_drawn_first_visible_ ||
        (last_drawn_state_ != DisplayState::Normal &&
         last_drawn_state_ != DisplayState::Stale);

    if (full_rows_repaint) {
        clearBody(tft);
    }

    for (uint8_t i = 0; i < kViewportRows; ++i) {
        int row_y = kBodyTopY + (int)i * kRowH;
        uint8_t idx = (uint8_t)((first_visible_ + i) % (total > 0 ? total : 1));
        if (i >= total) {
            // Empty viewport slot: repaint to background.
            tft.fillRect(0, row_y, kBodyW - kScrollW - 4, kRowH, kColBg);
            last_drawn_[i] = {};
            last_drawn_minute_[i] = INT32_MIN;
            continue;
        }

        const auto& svc = data_.services[idx];
        int minute = displayedMinute(now_ms, svc);

        bool row_changed =
            full_rows_repaint ||
            strncmp(svc.service_no, last_drawn_[i].service_no,
                    sizeof(svc.service_no)) != 0 ||
            svc.load != last_drawn_[i].load ||
            svc.type != last_drawn_[i].type;

        bool minute_changed = minute != last_drawn_minute_[i];

        if (row_changed) {
            // Repaint the whole row band.
            tft.fillRect(0, row_y, kBodyW - kScrollW - 4, kRowH, kColBg);

            // Service number — size 2.
            tft.setTextSize(2);
            tft.setTextColor(fg, kColBg);
            tft.setCursor(kCol_Service, row_y);
            tft.print(svc.service_no);

            // Load dot.
            tft.fillCircle(kCol_Dot + 5, row_y + 7, 5, loadColor(svc.load));

            // Load text.
            tft.setTextSize(1);
            tft.setTextColor(stale ? kColDim : loadColor(svc.load), kColBg);
            tft.setCursor(kCol_Load, row_y + 4);
            tft.print(loadLabel(svc.load));

            // Type tag.
            tft.setTextColor(stale ? kColDim : typeColor(svc.type), kColBg);
            tft.setCursor(kCol_Type, row_y + 4);
            tft.print(typeLabel(svc.type));

            last_drawn_[i] = svc;
        }

        if (row_changed || minute_changed) {
            // ETA cell only.
            tft.fillRect(kCol_Eta, row_y, kEtaW, kRowH, kColBg);
            tft.setTextSize(2);
            if (minute == INT32_MIN) {
                tft.setTextColor(kColDim, kColBg);
                tft.setCursor(kCol_Eta, row_y);
                tft.print("--");
            } else if (minute <= 0) {
                tft.setTextColor(kColEtaArr, kColBg);
                tft.setCursor(kCol_Eta, row_y);
                tft.print("Arr");
            } else if (minute >= 60) {
                tft.setTextColor(kColDim, kColBg);
                tft.setCursor(kCol_Eta, row_y);
                tft.print("60+");
            } else {
                tft.setTextColor(fg, kColBg);
                char m[8];
                snprintf(m, sizeof(m), "%dm", minute);
                tft.setCursor(kCol_Eta, row_y);
                tft.print(m);
            }
            last_drawn_minute_[i] = minute;
        }
    }

    // Scroll bar.
    if (total > kViewportRows) {
        // Track.
        tft.fillRect(kScrollX, kScrollY, kScrollW, kScrollH, kColBg);
        tft.drawRect(kScrollX, kScrollY, kScrollW, kScrollH, kColDivider);
        // Thumb.
        int thumb_h = (kScrollH * (int)kViewportRows) / (int)total;
        if (thumb_h < 4) thumb_h = 4;
        int thumb_y = kScrollY + (kScrollH * (int)first_visible_) / (int)total;
        tft.fillRect(kScrollX + 1, thumb_y + 1,
                     kScrollW - 2, thumb_h - 2, kColFg);
    } else {
        tft.fillRect(kScrollX, kScrollY, kScrollW, kScrollH, kColBg);
    }

    last_drawn_first_visible_ = first_visible_;
}
```

- [ ] **Step 2: Implement `renderScrollHint`**

Replace the stub:

```cpp
void BusCard::renderScrollHint(Adafruit_ST7789& tft, uint32_t now_ms) {
    if (data_.service_count <= kViewportRows) return;
    if (first_visible_ != 0) return;
    uint32_t shown_for = now_ms - shown_at_ms_;
    if (shown_for >= kHintShowMs) {
        // Erase the hint band on the bottom row if we previously drew it.
        // (Safe to fillRect even if nothing was there — paints bg over bg.)
        tft.fillRect(0, 135 - 10, 240, 10, kColBg);
        return;
    }
    tft.setTextSize(1);
    tft.setTextColor(kColDim, kColBg);
    const char* hint = "press [O] to scroll";
    int16_t x1, y1; uint16_t w1, h1;
    tft.getTextBounds(hint, 0, 0, &x1, &y1, &w1, &h1);
    int x = (240 - (int)w1) / 2;
    tft.setCursor(x, 135 - 9);
    tft.print(hint);
    // Stay dirty so we re-render and erase the hint after kHintShowMs.
    dirty_ = true;
}
```

- [ ] **Step 3: Build**

Run: `pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -10`

Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/ui/cards/BusCard.cpp
git commit -m "feat(ui): BusCard service rows, scroll bar, and one-shot scroll hint"
```

---

## Task 12: Wire `BusCard` into `CardController`

**Files:**
- Modify: `src/ui/CardController.h`
- Modify: `src/ui/CardController.cpp`

- [ ] **Step 1: Add `BusCard` to the controller header**

In `src/ui/CardController.h`, add the include after the other card includes (around line 19):

```cpp
#include "cards/BusCard.h"
```

Add four members near the other `*_card_` members (around line 70):

```cpp
    BusCard          bus_card_0_;
    BusCard          bus_card_1_;
    BusCard          bus_card_2_;
    BusCard          bus_card_3_;
```

- [ ] **Step 2: Construct the four `BusCard`s**

In `src/ui/CardController.cpp`, extend the initialiser list (around line 28). Insert after `factory_reset_card_(fr),`:

```cpp
      bus_card_0_(0, settings, wifi),
      bus_card_1_(1, settings, wifi),
      bus_card_2_(2, settings, wifi),
      bus_card_3_(3, settings, wifi),
```

- [ ] **Step 3: Update `cardForId` and `rebuildStack` to handle the new IDs**

Replace the `cardForId` helper (around line 71) with one that takes the controller reference so it can return the right `BusCard`. The simplest path is to make it a lambda inside `rebuildStack` or a private method. Replace the namespace helper and `rebuildStack` body (lines 71-130) with:

```cpp
namespace {
Card* cardForIdLegacy(uint8_t id, StatusCard& s, EyesCard& e,
                      WifiCard& w, NavTestCard& n) {
    switch (id) {
        case settings::CARD_STATUS:  return &s;
        case settings::CARD_EYES:    return &e;
        case settings::CARD_WIFI:    return &w;
        case settings::CARD_NAVTEST: return &n;
    }
    return nullptr;
}
}  // namespace

void CardController::rebuildStack() {
    const settings::Settings& d = settings_.data();

    if (last_cards_mask_         == d.cards_enabled_mask &&
        last_cards_order_count_  == d.cards_order_count &&
        last_boot_card_          == d.boot_card_id &&
        memcmp(last_cards_order_, d.cards_order, sizeof(last_cards_order_)) == 0 &&
        applied_boot_card_) {
        return;
    }

    auto cardFor = [this, &d](uint8_t id) -> Card* {
        Card* legacy = cardForIdLegacy(id, status_card_, eyes_card_,
                                        wifi_card_, nav_card_);
        if (legacy) return legacy;
        if (id >= settings::CARD_BUS_1 && id <= settings::CARD_BUS_4) {
            uint8_t slot = id - settings::CARD_BUS_1;
            // Hide the card if its slot is empty even when the mask bit
            // happens to be set (defence-in-depth alongside settings
            // validation).
            if (d.bus_stops[slot].code[0] == '\0') return nullptr;
            switch (slot) {
                case 0: return &bus_card_0_;
                case 1: return &bus_card_1_;
                case 2: return &bus_card_2_;
                case 3: return &bus_card_3_;
            }
        }
        return nullptr;
    };

    Card* prev_active = stack_.active();
    uint8_t prev_id = 0xFF;
    for (uint8_t i = 0; i < last_cards_order_count_; ++i) {
        Card* c = cardFor(last_cards_order_[i]);
        if (c == prev_active) { prev_id = last_cards_order_[i]; break; }
    }

    stack_.clear();
    for (uint8_t i = 0; i < d.cards_order_count; ++i) {
        Card* c = cardFor(d.cards_order[i]);
        if (c) stack_.addCard(c);
    }

    uint8_t target_id = applied_boot_card_
        ? (prev_id != 0xFF ? prev_id : d.boot_card_id)
        : d.boot_card_id;
    size_t target_index = 0;
    for (uint8_t i = 0; i < d.cards_order_count; ++i) {
        if (d.cards_order[i] == target_id) { target_index = i; break; }
    }
    stack_.setIndex(target_index);

    last_cards_mask_         = d.cards_enabled_mask;
    last_cards_order_count_  = d.cards_order_count;
    memcpy(last_cards_order_, d.cards_order, sizeof(last_cards_order_));
    last_boot_card_          = d.boot_card_id;
    applied_boot_card_       = true;
}
```

- [ ] **Step 4: Verify `last_cards_order_` is sized for 8 entries**

In `src/ui/CardController.h`, the existing line is:

```cpp
    uint8_t      last_cards_order_[settings::CARD_COUNT];
```

`CARD_COUNT` is now 8, so this auto-sizes correctly. No change needed.

- [ ] **Step 5: Build**

Run: `pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -10`

Expected: build succeeds.

- [ ] **Step 6: Commit**

```bash
git add src/ui/CardController.h src/ui/CardController.cpp
git commit -m "feat(ui): instantiate four BusCards in CardController and route by id"
```

---

## Task 13: HTTP — extend `/api/settings` JSON (already covered by toJson; no change to handler)

The existing `/api/settings` handler in `src/net/HttpServer.cpp` (around line 691) calls `settings::toJson` directly. Task 3 extended `toJson` to include `bus_stops`, so this endpoint already returns the new field. No code change in this task; it exists only as a checkpoint to confirm.

- [ ] **Step 1: Confirm by reading the existing handler**

Run: `grep -n -A 8 '"/api/settings", HTTP_GET' src/net/HttpServer.cpp`

Expected: a one-shot `toJson(settings_.data(), buf, sizeof(buf))` write. If yes, nothing to do.

- [ ] **Step 2: No commit** (no code change). Proceed to Task 14.

---

## Task 14: HTTP — `POST /api/settings/bus-stops`

**Files:**
- Modify: `src/net/HttpServer.cpp`

- [ ] **Step 1: Add the endpoint**

In `src/net/HttpServer.cpp`, immediately after the `/api/settings/cards` handler block (around line 784, inside `registerStaHandlers()`), add:

```cpp
    // ---- /api/settings/bus-stops
    server_->on("/api/settings/bus-stops", HTTP_POST, [this]() {
        if (!server_->hasArg("slot")) {
            sendJsonError(server_, 400, "missing field");
            return;
        }
        long slot_long = server_->arg("slot").toInt();
        if (slot_long < 0 || slot_long >= settings::MAX_BUS_STOPS) {
            sendJsonError(server_, 400, "slot out of range");
            return;
        }
        // code and label are both optional in the form payload but always
        // present as args (possibly empty). Accept missing as empty.
        String code  = server_->hasArg("code")  ? server_->arg("code")  : String("");
        String label = server_->hasArg("label") ? server_->arg("label") : String("");
        char err[64] = {};
        if (!settings_.applyBusStop(static_cast<uint8_t>(slot_long),
                                    code.c_str(), label.c_str(),
                                    err, sizeof(err))) {
            sendJsonError(server_, 400, err);
            return;
        }
        sendJsonOk(server_);
    });
```

- [ ] **Step 2: Add the include for `settings::MAX_BUS_STOPS` if needed**

The handler references `settings::MAX_BUS_STOPS`. `HttpServer.cpp` already includes `Settings.h`, which transitively includes `settings_model.h`, so the symbol is in scope. No new include needed.

- [ ] **Step 3: Build**

Run: `pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -10`

Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/net/HttpServer.cpp
git commit -m "feat(http): POST /api/settings/bus-stops applies a single slot"
```

---

## Task 15: HTTP — extend the embedded HTML form

**Files:**
- Modify: `src/net/HttpServer.cpp`

- [ ] **Step 1: Add the HTML section**

In `src/net/HttpServer.cpp`, find the closing of the Cards `<div class=section>` (around line 348, the `"</div>"` immediately before `// -------- NETWORK --------`). Insert the new section between the two:

```cpp
            // -------- BUS STOPS --------
            "<div class=section>"
              "<h2>Bus stops</h2>"
              "<form id=bus-form onsubmit='return saveBusStops(event)'>"
                "<table class=bus-tbl style='width:100%;border-spacing:0 .4rem'>"
                  "<thead><tr style='text-align:left;font-size:.8rem;color:var(--muted)'>"
                    "<th>Slot</th><th>Code (5 digits)</th><th>Label</th>"
                  "</tr></thead>"
                  "<tbody id=bus-tbody></tbody>"
                "</table>"
                "<button type=submit>Save bus stops</button>"
                "<p id=bus-msg class=status-msg></p>"
              "</form>"
            "</div>"
```

- [ ] **Step 2: Add the JS that loads + saves the bus stops**

In the `<script>` block, find `loadSettings()` (around line 433). At the end of its body (just before the closing `}`), append:

```cpp
              "renderBusStops(s.bus_stops||[]);"
```

Then add the new render + save functions after `renderBoot()` (around line 479):

```cpp
            "function renderBusStops(slots){"
              "const tb=$('bus-tbody');tb.innerHTML='';"
              "for(let i=0;i<4;i++){"
                "const s=slots.find(x=>x.slot===i)||{slot:i,code:'',label:''};"
                "const tr=document.createElement('tr');"
                "tr.innerHTML="
                  "'<td style=\"padding:.2rem .4rem\">'+(i+1)+'</td>'+"
                  "'<td><input data-slot='+i+' class=bus-code type=text "
                    "inputmode=numeric pattern=\"[0-9]{5}\" maxlength=5 "
                    "placeholder=\"empty=disabled\" value=\"'+s.code+'\"></td>'+"
                  "'<td><input data-slot='+i+' class=bus-label type=text "
                    "maxlength=12 placeholder=\"optional\" value=\"'+"
                    "(s.label||'').replace(/\"/g,'&quot;')+'\"></td>';"
                "tb.appendChild(tr);"
              "}"
            "}"
            "async function saveBusStops(ev){"
              "ev.preventDefault();"
              "let any_err=null;"
              "for(let i=0;i<4;i++){"
                "const code=document.querySelector('.bus-code[data-slot=\"'+i+'\"]').value.trim();"
                "const label=document.querySelector('.bus-label[data-slot=\"'+i+'\"]').value;"
                "const fd=new FormData();fd.append('slot',i);"
                "fd.append('code',code);fd.append('label',label);"
                "const{ok,j}=await jpost('/api/settings/bus-stops',fd);"
                "if(!ok){any_err=j.error||'failed';break}"
              "}"
              "setMsg($('bus-msg'),any_err?any_err:'saved.',any_err?'err':'ok');"
              "if(!any_err)await loadSettings();"
              "return false"
            "}"
```

- [ ] **Step 3: Build**

Run: `pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -10`

Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/net/HttpServer.cpp
git commit -m "feat(http): web UI section to view/edit four bus-stop slots"
```

---

## Task 16: Default cards UI — surface the new bus cards in the existing Cards section

The existing Cards section iterates `s.cards` (see `renderCards`, around line 449), which now includes the four bus cards because Task 3 extended `toJson`. They will appear automatically with the names "Bus 1".."Bus 4" and `enabled` defaulted to false. No code change required — verified visually in Task 26.

- [ ] **Step 1: No commit. Move on to Task 17.**

---

## Task 17: On-device manual validation

This is the only verification path for the Arduino-side render and fetch behaviour. Run after every task completion that touches device code, but the full-stack pass is here.

**Setup:**
1. Build & flash: `pio run -e adafruit_feather_esp32s3_reversetft -t upload && pio device monitor -e adafruit_feather_esp32s3_reversetft`.
2. Connect device to known Wi-Fi (existing flow). Confirm `claude.local` reaches the web UI.

- [ ] **Step 1: Configure one bus stop**

In the web UI: Bus stops section → Slot 1 → Code `50171`, Label `Home`. Save. In the Cards section, tick "Bus 1", arrange order to first, save. Reboot via the Reboot action.

Expected at boot: a card with header "Home" appears as the visible card.
Within ~2 s: services list populates with at least one row.

- [ ] **Step 2: Confirm 30 s refetch + minute tick**

Watch the serial monitor. Every 30 s a request to `api.busaunty.com` should appear (or at minimum, a `[boot]`-style log line when the fetcher fires; if the fetcher does not currently log, add a one-line `Serial.printf("[bus] fetch slot=%u code=%s status=%d\n", slot_, code, ok ? 0 : -1);` to `BusArrivalsFetcher::fetch` for the duration of testing).

Watch the screen: each minute that passes between fetches, the displayed minute should decrement on its own without the screen strobing.

- [ ] **Step 3: Confirm the no-fillScreen rule on minute tick**

Watch the screen carefully when a minute boundary crosses. Only the ETA cell of the affected row should change; the rest of the screen must not flash black. (Per the project CLAUDE.md rule.)

- [ ] **Step 4: Configure four bus stops**

Set up slots 1-4. Confirm: four cards appear in the carousel; flipping between them via UP/DOWN works; only the visible card logs network activity.

- [ ] **Step 5: Drop and restore Wi-Fi**

While on a bus card, disconnect the Wi-Fi (e.g., switch off the AP). Within one tick the card should switch to the "No Wi-Fi" centre message. Restore Wi-Fi; the next fetch within 30 s should populate the rows.

- [ ] **Step 6: Pick a stop with > 7 services**

Use stop code `50171` (which currently shows 7) or find a busier interchange (e.g. `46211`). Confirm: scroll bar appears on the right; CENTER advances by one row; wrapping back to the top works.

- [ ] **Step 7: Hint banner**

When you flip to a card with > 7 services, the "press [O] to scroll" hint should appear at the bottom of the screen for ~3 s, then disappear without leaving artefacts.

- [ ] **Step 8: Bad code handling**

In the web UI, set Slot 2 to an obviously invalid code that the API will return zero services for, e.g. `00000`. Save. Flip to that card. Expected: "No services found for stop 00000" centred on the body. (If the API instead returns an HTTP 4xx, the screen reads "Stop 00000 unknown — check the web UI". Either is acceptable; record which one the live API does.)

- [ ] **Step 9: Settings migration**

If you have an existing device with the legacy v1 settings blob (an upgrade rather than a fresh flash): on first boot, the device should retain its `device_name`, brightness, etc. and have all four bus slots empty. The next save in the web UI should write the v2 blob.

- [ ] **Step 10: Power off / wake-up**

Trigger sleep (idle past dim then sleep timeout, or via the existing test path). On wake, the visible card should re-render fully (the existing `invalidate()` path); minute counters should still be roughly correct since `millis()` is monotonic.

- [ ] **Step 11: Remove debug log**

If a `Serial.printf` was added in Step 2 for log visibility, remove it now.

- [ ] **Step 12: Commit any cleanup**

```bash
git status
# If only the debug log was removed:
git add src/net/BusArrivalsFetcher.cpp
git commit -m "chore(bus): remove temporary fetch log"
```

---

## Self-Review

**Spec coverage:**

| Spec section | Implemented in |
|---|---|
| Goals (1 stop card per slot, render rules, local tick, CENTER scroll, bounded fetch) | Tasks 5–12, 17 |
| Non-goals (no NTP, no background refresh, no alerts) | Honoured by design — `tick()` returns early when `!visible_` (Task 9); no NTP code; no notification path |
| API shape | Task 6 parser tests cover the live shape including fractional UpdatedAt |
| UX: visible state, scroll bar, hint | Task 11 |
| UX: six display states | Tasks 9 (compute) + 10 (Loading/NoWifi/Empty/BadCode/FetchError) + 11 (Stale via dim rendering) |
| Settings schema (`BusStopSlot`, IDs 4–7, validation, NVS migration) | Tasks 1–4 |
| HTTP endpoints (`/api/settings` extension, POST `/api/settings/bus-stops`) | Tasks 3 + 13 + 14 |
| Web UI HTML | Task 15 |
| Networking (`setInsecure`, body cap, 8 s timeout) | Task 8 |
| Overflow surfaces (filter, count cap, strncpy + null term, enum parse, sentinel, timeout) | Task 6 (parse), Task 8 (HTTP body cap), Task 9 (BusCard reads bounded) |
| `BusCard` data structures | Task 5 (header) + Task 9 (card-private state) |
| Card framework (`onShow`/`onHide`) | Task 7 |
| Render strategy (no `fillScreen` on minute tick) | Task 11 (per-cell `fillRect`) verified in Task 17 Step 3 |
| `CardController` integration | Task 12 |
| Tests (host-side parse, settings, ISO) | Tasks 2, 3, 5, 6 |
| On-device manual checklist | Task 17 |

**Placeholder scan:** none. Each step shows the actual code to write or paste.

**Type consistency:** `BusServiceArrival`, `BusStopArrivals`, `BusLoad`, `BusType`, `kViewportRows`, `kMaxServicesPerStop`, `kMaxResponseBytes`, `parseIso8601Delta`, `parseBusArrivalsJson`, `applyBusStopField`, `Settings::applyBusStop` — all names declared once and used consistently across tasks.

**Spec gaps:** none. The `Bad stop code` state's exact trigger (HTTP 400/404) requires a tweak in `BusArrivalsFetcher::fetch` to distinguish from generic 5xx — currently any non-200 maps to `FetchError` via `last_error = "http NNN"`. Task 17 Step 8 makes this explicit and documents whichever path the live API takes; if the live API never returns 400/404 for unknown stops (returns empty `Services` instead), the `BadCode` state is unreachable and that's fine — `Empty` covers it.

---
