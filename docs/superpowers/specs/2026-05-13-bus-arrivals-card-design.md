# Bus Arrivals Card — Design

**Status:** approved 2026-05-13
**Owner:** Lino

## Summary

Add a new family of cards to Claude Buddy that show next-bus arrival times for up to four user-configured Singapore bus stops. Each configured stop becomes its own sibling card in the existing card carousel, fetching from a public bus-arrivals JSON API every 30 s while visible.

## Goals

- A user configures one to four bus stops in the existing web UI (each: 5-digit stop code + optional friendly label).
- Each configured stop appears as its own card in the carousel; UP/DOWN flips between cards as today.
- The visible bus card shows, per service at that stop: service number, the next bus's ETA in minutes, a coloured load indicator (seats / standing / limited), and bus type (single-deck / double-deck / bendy).
- The displayed minute counts down on its own between fetches; no NTP required.
- The CENTER button scrolls services within a card when more services exist than fit on screen.
- All overflow surfaces in the fetch and parse path are bounded; an unexpectedly large or malformed response cannot crash or corrupt the device.

## Non-goals

- No multi-stop combined view; no "next bus across stops" widget elsewhere.
- No NTP / wall-clock time. We use server-supplied timestamps reduced to "seconds-from-fetch" and `millis()` for the local tick.
- No background refresh while the card is hidden.
- No alerts or notifications when a bus is "Arr".
- No support for non-Singapore APIs in v1. The fetch URL and JSON parse are specific to `api.busaunty.com`'s shape.

## API

`GET https://api.busaunty.com/api/v1/BusArrival?BusStopCode=NNNNN`

Public, unauthenticated, HTTPS. Returns a single-stop response of this shape (sample captured 2026-05-13):

```json
{
  "busStops": [
    {
      "BusStopCode": 50171,
      "Services": [
        {
          "ServiceNo": "21",
          "NextBus":  {"EstimatedArrival": "2026-05-13T17:53:41+08:00",
                       "Load": "SEA", "Feature": "WAB", "Type": "DD"},
          "NextBus2": {"EstimatedArrival": "...", ...},
          "NextBus3": {"EstimatedArrival": "...", ...}
        }
      ],
      "UpdatedAt": "2026-05-13T17:47:51.46162+08:00"
    }
  ]
}
```

The endpoint also supports chaining multiple `BusStopCode` query params for up to four stops in a single response, but **this design queries one stop per card** (each card is independent and only the visible one fetches).

`Load` ∈ {`SEA`=Seats Available, `SDA`=Standing Available, `LSD`=Limited Standing}.
`Type` ∈ {`SD`=Single-Deck, `DD`=Double-Deck, `BD`=Bendy}.
`Feature` is parsed but unused in v1 (reserved for a future wheelchair-accessibility glyph).

## UX

### Visible state, normal

Header: user-supplied label (or `Stop NNNNN` if empty) on the left, divider below. No wall-clock time shown — there is no NTP and no other reliable wall clock on the device. Per-row ETAs convey freshness on their own.
Up to `kViewportRows = 7` service rows in the body. Each row:

```
  21    ●SEA       5 min      [DD]
```

- Service number, size-2 font, left-justified at x=8.
- Filled 10 px circle, coloured by `Load`: green = SEA, amber = SDA, red = LSD, grey outline only = unknown.
- Load code text in size-1 font next to the circle.
- ETA in size-2 font, right-anchored to `kEtaX + kEtaW`. `≤ 0 min` renders as `"Arr"` in yellow; `1..59` as `"<n> min"`; `≥ 60` as `"60+ min"` in dim grey (rare; end-of-service edge).
- Type tag in size-1 font: `[DD]` cyan, `[SD]` light grey, `[BD]` magenta.

When a stop has more than `kViewportRows` services, a vertical scroll bar is drawn on the right margin (x=234..238). The thumb height is `viewport / total` of the track; the thumb position is `first_visible / total`. CENTER advances `first_visible` by one row, wrapping to zero after the last row. On every `onShow()` where `service_count > kViewportRows` and `first_visible == 0`, a small footer hint `press ◉ to scroll` is shown for ~3 s, then erased.

### Header label

User-supplied label (≤12 chars, free text). Defaults to `Stop NNNNN` when the user leaves the label blank. A label change in the web UI repaints the header on the next tick.

### Local minute tick

Each service row holds an integer `eta_seconds_at_fetch` reduced from `(EstimatedArrival - UpdatedAt)` at fetch time. The renderer recomputes the displayed minute as `floor((eta_seconds_at_fetch - (millis() - fetched_at_ms)/1000) / 60)`. When that integer changes for any visible row, that row's ETA cell is repainted (a tight `fillRect` over the ETA span — see render strategy below). Per CLAUDE.md, the per-minute path never calls `fillScreen`.

### Refresh cadence

While the card is the visible top of the stack: HTTP fetch every 30 s. On `onShow()`, an immediate fetch is scheduled for the next tick. On `onHide()`, the schedule is cleared and the network is left alone.

### Six display states

| State | Trigger | Render |
|---|---|---|
| Loading | First-ever fetch in flight, `data_.valid == false`, `last_error[0] == '\0'` | Centre `"Loading…"` plus dim Wi-Fi IP underneath |
| No Wi-Fi | `wifi.isConnected() == false` | Centre `"No Wi-Fi"` plus `"Configure at claude.local"` |
| Stale | `data_.valid == true` and `last_error[0] != '\0'` and `(now_ms - last_fetch_success_ms) > 90000` | Prior data dimmed mid-grey; `⚠` glyph in header (right side); footer `"last update Nm ago"`. Minute tick continues over the dim values. |
| Empty stop | Fetch OK, `service_count == 0` | Centre `"No services found for stop NNNNN"` |
| Bad stop code | HTTP 400 / 404 | Centre `"Stop NNNNN unknown — check the web UI"` |
| Other fetch error | HTTP timeout / 5xx / parse error, `data_.valid == false` | Centre `"Bus times unavailable"` plus a small `last_error` line |

`last_error` is the empty string after every successful fetch and a short non-empty string after every failed fetch. `last_fetch_success_ms` is the `millis()` value of the most recent successful fetch (separate from `last_fetch_ms_`, which tracks attempts).

State transitions are `full_clear = true` repaints (rare; `fillScreen` is fine here).

## Settings & web UI

### Schema additions (in `lib/settings/settings_model.h`)

```c
constexpr uint8_t  MAX_BUS_STOPS     = 4;
constexpr uint8_t  MAX_BUS_LABEL_LEN = 12;   // not counting null
constexpr uint8_t  BUS_STOP_CODE_LEN = 5;

struct BusStopSlot {
    char code[BUS_STOP_CODE_LEN + 1];   // "" = empty slot, disables card
    char label[MAX_BUS_LABEL_LEN + 1];  // "" = fall back to "Stop NNNNN"
};

// added to struct Settings:
BusStopSlot bus_stops[MAX_BUS_STOPS];
```

### `CardId` extension

```c
enum CardId : uint8_t {
    CARD_STATUS  = 0,
    CARD_EYES    = 1,
    CARD_WIFI    = 2,
    CARD_NAVTEST = 3,
    CARD_BUS_1   = 4,
    CARD_BUS_2   = 5,
    CARD_BUS_3   = 6,
    CARD_BUS_4   = 7,
    CARD_COUNT   = 8,
};
```

`cards_enabled_mask` (uint8_t) now uses all eight bits — exactly the ceiling. `cards_order[CARD_COUNT]` grows from 4 to 8 bytes.

### Validation

- Each `bus_stops[i].code` is empty or exactly five ASCII digits. Reject anything else with a clear error.
- `label` is null-terminated within `MAX_BUS_LABEL_LEN`. No control characters.
- `cards_enabled_mask` bit for `CARD_BUS_n` is **auto-cleared** during apply if the corresponding slot's `code` is empty (forgiving rather than rejecting).

### NVS migration

`Settings::begin()` already calls `setDefaults` and overlays the persisted blob. Migration:
- If the read returns the **legacy size** (pre-bus-stops) and `cards_order_count <= 4` (legacy CARD_COUNT), accept the read; the new `bus_stops[]` and `cards_order[4..7]` stay zero-initialised.
- Otherwise expect the new size.
- On the next `persist()`, the new blob shape is written.

### HTTP endpoints

- `GET /api/settings` — extended to include `bus_stops` array.
- `POST /api/settings/bus-stops` — accepts `slot=0..3`, `code=NNNNN|""`, `label=...`. Implemented as `Settings::applyBusStop(uint8_t slot, const char* code, const char* label, char* err, size_t err_len)`, mirroring `applyDailyCap`. Fires `EventKind::SettingsChanged` on success.
- The existing `POST /api/settings/cards` is unchanged; the bus card IDs are simply additional valid IDs in the mask and order arrays.

### Web UI HTML (in `HttpServer.cpp`)

Append a `<section>` titled "Bus stops" with four slot rows:

```
─ Bus stops ──────────────────────────────────────
  Slot 1   [Code: 50171     ]  [Label: Home   ]   ☑ Enabled
  Slot 2   [Code: 54321     ]  [Label: Office ]   ☑ Enabled
  Slot 3   [Code:           ]  [Label:        ]   ☐ Enabled (disabled while code empty)
  Slot 4   [Code:           ]  [Label:        ]   ☐ Enabled

  [ Save bus stops ]
```

The save button posts each slot in turn to `/api/settings/bus-stops`. Reuses existing `jget`/`jpost` helpers.

## Networking

Mirrors `src/net/GitHubReleases.cpp`'s `HTTPClient` + `NetworkClientSecure` shape, with one critical difference:

```cpp
NetworkClientSecure client;
client.setInsecure();   // public, unauthenticated data; no CA bundle needed
HTTPClient http;
http.setTimeout(8000);
http.begin(client, url);
```

No `data/*.pem` bundle, no objcopy step. The OTA path keeps its full CA bundle as-is. (See feedback memory: HTTPS effort proportional to threat model.)

URL: `https://api.busaunty.com/api/v1/BusArrival?BusStopCode=NNNNN` — one stop per card.

### Overflow surfaces

| Surface | Defense |
|---|---|
| TLS cert validation | `setInsecure()` (data is public, no auth) |
| HTTP body size | `http.getSize()` check + streaming abort at `kMaxResponseBytes = 32 KB` |
| JSON parse memory | ArduinoJson `Filter` keeps only rendered fields |
| Service count | hard cap `kMaxServicesPerStop = 16`, silently truncate beyond |
| Per-string copy | `strncpy(dst, src, sizeof(dst)-1); dst[sizeof(dst)-1]='\0';` everywhere |
| Enum strings (`Load`, `Type`) | parsed into enum via lookup table; never stored as char |
| Malformed ISO timestamp | sentinel `INT32_MIN`; renderer shows `"—"` for that row |
| Fetch timeout | 8 s; sets `last_error`, surfaces "stale" overlay on next render |

### Filter spec

```cpp
JsonDocument filter;
filter["busStops"][0]["Services"][0]["ServiceNo"] = true;
filter["busStops"][0]["Services"][0]["NextBus"]["EstimatedArrival"] = true;
filter["busStops"][0]["Services"][0]["NextBus"]["Load"] = true;
filter["busStops"][0]["Services"][0]["NextBus"]["Type"] = true;

JsonDocument doc;
auto err = deserializeJson(doc, http.getStream(),
                           DeserializationOption::Filter(filter));
```

Drops `Feature` (unused in v1), `NextBus2`/`NextBus3` (we render only the next bus), `BusStopCode` (already known), `UpdatedAt` (we use `millis()` deltas). Roughly five-times memory reduction vs an unfiltered parse.

### `parseIso8601Delta`

Pure C++, no allocation. Parses `YYYY-MM-DDTHH:MM:SS+HH:MM` into a `time_t` via fixed-position `sscanf` with width specifiers, then computes `(EstimatedArrival_unix - UpdatedAt_unix)` as the per-row `eta_seconds_at_fetch`. Returns `INT32_MIN` on any parse failure. Bounded by `strnlen` first.

## Data structures

```cpp
enum BusLoad : uint8_t { LOAD_UNKNOWN = 0, LOAD_SEA, LOAD_SDA, LOAD_LSD };
enum BusType : uint8_t { TYPE_UNKNOWN = 0, TYPE_SD,  TYPE_DD,  TYPE_BD  };

struct BusServiceArrival {
    char    service_no[5];          // up to "961M\0"
    int32_t eta_seconds_at_fetch;   // INT32_MIN = no data
    BusLoad load;
    BusType type;
    bool    wab;                    // parsed but not rendered v1
};

struct BusStopArrivals {
    BusServiceArrival services[16];
    uint8_t           service_count;
    uint32_t          fetched_at_ms;          // millis() at most recent successful parse
    uint32_t          last_fetch_success_ms;  // == fetched_at_ms; named for staleness checks
    bool              valid;                  // true once at least one fetch has succeeded
    char              last_error[32];         // "" iff most recent attempt succeeded
};
```

## `BusCard` class

```cpp
class BusCard : public Card {
public:
    BusCard(uint8_t slot_index, const Settings& settings,
            const WifiManager& wifi);

    void invalidate() override;
    bool isDirty() const override;
    void render(Display& display) override;
    void tick(uint32_t now_ms) override;
    bool handleButton(ButtonEvent ev, uint32_t now_ms) override;
    void onShow() override;            // sets visible_, schedules immediate fetch
    void onHide() override;            // clears visible_, leaves data_ as-is

private:
    uint8_t              slot_;
    const Settings&      settings_;
    const WifiManager&   wifi_;
    BusStopArrivals      data_;
    uint32_t             last_fetch_ms_;
    uint32_t             last_tick_minute_;
    uint8_t              first_visible_;
    bool                 ever_drawn_;
    bool                 visible_;
    BusServiceArrival    last_drawn_[7];
    uint8_t              last_drawn_first_visible_;
    char                 last_drawn_label_[MAX_BUS_LABEL_LEN + 1];
    char                 last_drawn_code_[BUS_STOP_CODE_LEN + 1];
    bool                 last_drawn_wifi_up_;
    bool                 last_drawn_valid_;
    char                 last_drawn_error_[32];
};
```

### Visibility signal (new on `Card`)

```cpp
// Card.h additions:
virtual void onShow() {}
virtual void onHide() {}
```

`CardStack` calls `onHide()` on the prior top and `onShow()` on the new top on every push, pop, or top change. Existing cards keep the empty defaults; this also lets `StatusCard` drop its periodic-recheck workaround in a follow-up.

### Tick logic

```
if (!visible_) return;
if (!wifi.isConnected()) {
    if (last_drawn_wifi_up_) mark_dirty;   // flip to "No Wi-Fi"
    return;
}
if (!data_.valid || (now_ms - last_fetch_ms_ >= 30000)) {
    runFetch();           // synchronous; updates data_, last_fetch_ms_
    mark_dirty;
    return;
}
uint32_t minute = (now_ms - data_.fetched_at_ms) / 60000;
if (minute != last_tick_minute_) {
    last_tick_minute_ = minute;
    mark_dirty_eta_cells_only;
}
```

### Button handling

```
constexpr uint8_t kViewportRows = 7;

if (ev == BTN_CENTER && data_.service_count > kViewportRows) {
    first_visible_ = (first_visible_ + 1) % data_.service_count;
    mark_dirty;
    return true;       // consume
}
return false;          // fall through to nav
```

## Render strategy

Three classes of repaint, per CLAUDE.md's no-`fillScreen`-in-continuous-animations rule:

- **State transitions (`full_clear = true`):** one-shot `fillScreen` on `onShow()`, on Wi-Fi flip, on header label change, on transitions between any of the six display states. Drawn rarely — `fillScreen` cost is fine.
- **Per-frame increments (`full_clear = false`):** ETA cells aging at minute boundaries. Each row's ETA cell is a fixed rect (`fillRect(kEtaX, row_y, kEtaW, kRowH, BLACK)` then `drawEta`). ~1 KB SPI per row, sub-millisecond.
- **Per-fetch row diffs:** when fetch returns and `last_drawn_[i]` differs from live, repaint only the changed row band (no `fillScreen`). Header repainted only if time-of-day text or the label/code changed. Scroll changes repaint all seven row bands.

`isDirty()` returns true if: visibility flipped, Wi-Fi state flipped, `data_.valid` flipped, `last_error` flipped between empty and non-empty, `first_visible_` changed, any per-row snapshot diverges from live, header label/code changed, or any displayed minute aged.

## CardController integration

`CardController` gains four `BusCard` members constructed with slot indices 0..3, the shared `Settings&`, and `WifiManager&`. `rebuildStack()`'s candidate map keys `CARD_BUS_1..CARD_BUS_4` to `bus_card_[0..3]`. A bus card is pushed only if its mask bit is set **and** its slot's `code[0] != '\0'`.

`InputRouter` is unchanged. Buttons already route through `CardStack::handleButton`, which delegates to the top card first; consuming `BTN_CENTER` for scroll requires no plumbing changes.

## Testing

### Host-side unit tests (`test/`)

1. `test_iso8601_parser.cpp` — valid timestamps, edge offsets, malformed inputs (returns `INT32_MIN`).
2. `test_bus_arrivals_parse.cpp` — feeds canned JSON through the parse pipeline:
   - Live 7-service capture → all fields land correctly.
   - 25-service stop → first 16 land, rest dropped, no overflow.
   - Missing `Load`/`Type` → enum lands as `UNKNOWN`.
   - Truncated JSON mid-document → parse error, `data_.valid` stays false.
   - Empty `busStops` → `service_count == 0`.
   - Body > `kMaxResponseBytes` (mocked) → "response too large", no parse attempted.
3. `test_settings_bus_stops.cpp`:
   - `applyBusStop` with valid 5-digit code → persisted, mask bit allowed.
   - With empty code → mask bit auto-cleared.
   - With non-numeric / wrong-length → rejected, error message set, state unchanged.
   - Label too long → rejected.
   - Round-trip: defaults → write all 4 → read back → identical.
4. `test_eta_render_math.cpp` — given `eta_seconds_at_fetch` + simulated `(now - fetched_at_ms)`, returns the correct minute label including the `"Arr"` boundary at exactly 0 s.
5. `test_settings_migration.cpp` — feeds a legacy-sized blob into `Settings::begin`, asserts new fields zero-init and existing fields preserved.

### On-device manual checklist

- Configure 1 stop → card appears, fetches, renders within 2 s of becoming visible.
- Configure 4 stops → 4 cards in carousel; only the visible one shows network activity in the serial log.
- Drop Wi-Fi mid-view → "No Wi-Fi" within one tick.
- Restore Wi-Fi → fetch resumes, no `fillScreen` strobe on per-minute tick.
- Stop with >7 services → CENTER scrolls, wraps cleanly, ETAs continue ticking on hidden rows (verified by scrolling back).
- Web UI: invalid code surfaces inline error; saving with empty code disables that card.

## Open questions

None at design time. (Wheelchair-accessibility glyph deferred to v2; non-Singapore APIs deferred indefinitely.)
