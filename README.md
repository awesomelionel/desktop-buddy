# claude-buddy

A minimal ESP32-S3 firmware for the [Adafruit Feather ESP32-S3 Reverse TFT](https://www.adafruit.com/product/5691) that connects to Claude Desktop over BLE and displays live session state on the built-in 1.14" TFT display.

This is a ground-up rewrite. See `cdb/REFERENCE.md` for the wire protocol; `cdb/` is the reference implementation.

## Features

### Display
- **4-state visual indicator** — shows `DISCONNECTED`, `IDLE`, `WORKING`, or `WAITING` in large centered text
- **Session counters** — total / running / waiting session counts updated in real-time
- **Status message** — last one-line summary from Claude Desktop (e.g. `approve: Bash`), auto-wrapped across 2 lines
- **Connection footer** — green `LIVE` / red `OFFLINE` badge with device name, based on heartbeat keepalive

### BLE / Connectivity
- **Nordic UART Service (NUS)** bridge — standard BLE UART profile for broad host compatibility
- **Auto-advertising** — device advertises as `Claude-XXXX` (MAC-derived suffix) on boot and after disconnect
- **Auto-reconnect** — restarts advertising automatically on BLE disconnect
- **4 KB ring + line buffers** — handles max event payloads and MTU fragmentation cleanly
- **30-second offline detection** — transitions to `OFFLINE` if no snapshot received within timeout

### Permission Prompts
When Claude Desktop blocks on a permission decision (`prompt` field present in a snapshot), the device switches to a full-screen decision UI:

- **Three options, vertical stack:** `Approve` (top, default highlight) / `Deny` / `Dismiss`.
- **Three onboard buttons** drive the UI — only active while a prompt is on screen:
  - **Up (D2)** / **Down (D0)** — move highlight, no wrap-around.
  - **Center (D1)** — confirm the highlighted option.
- **Approve / Deny** send `{"cmd":"permission","id":"...","decision":"once"|"deny"}` over the BLE TX characteristic. **Dismiss** sends nothing — it just clears the prompt UI locally.
- **`SENT: APPROVE` / `SENT: DENY` / `DISMISSED`** flashes for ~500 ms after a press, then the prompt UI hides.
- **Sticky dismiss:** once a prompt id is dismissed, the UI does not re-appear for that same id even if the desktop keeps sending it. A *new* prompt id will show again.
- **Auto-dismiss:** if the next snapshot drops the `prompt` field (someone decided elsewhere) or the link goes `OFFLINE`, the prompt UI clears immediately. Any in-flight press is discarded.
- **Footer preserved:** the `LIVE` / `OFFLINE` badge and device name remain visible on the prompt screen so connection state is never hidden.

#### Edge cases / callouts

- **Press-then-disconnect race.** If the user presses a decision and the link drops within the same loop tick, the BLE write fails and the response is silently dropped. The desktop's session manager will re-prompt on the next snapshot — no retry on the device.
- **No decision queueing.** Only one outgoing decision can be pending at a time. A second press before the first is drained overwrites it; in practice the 20 ms loop tick drains it long before another press is possible.
- **Buttons inert outside an active prompt.** Presses while no prompt is on screen (or while the prompt is dismissed) are ignored.

### Session State Monitoring
Receives and parses newline-delimited JSON snapshots from Claude Desktop containing:
- Running / waiting / total session counts
- One-line status message
- Cumulative and daily output token counts
- Recent transcript lines
- 10-second heartbeat keepalive

### State Machine
| State | Condition |
|---|---|
| `DISCONNECTED` | No BLE connection or heartbeat timeout |
| `IDLE` | Connected, no active or waiting sessions |
| `WORKING` | One or more sessions generating responses |
| `WAITING` | One or more sessions blocked on a permission prompt (takes priority over `WORKING`) |

### Protocol Parsing
- ArduinoJson 7-based parser with partial-update support — missing fields preserve previous state
- Malformed or non-JSON lines silently ignored
- Message strings truncated to 32-byte fixed buffer to keep RAM usage bounded

### Testing
- Unity-based host test suite (`pio test -e native`)
- 5 protocol parser tests: full snapshot, partial update, validation, error handling, truncation
- 6 state derivation tests: all four states + name mappings

## Hardware

| Component | Part |
|---|---|
| MCU + display | Adafruit Feather ESP32-S3 Reverse TFT (#5691) |
| Display | 1.14" ST7789 TFT, 135×240 px |
| Input | 3 onboard buttons — D2 (up), D0 (down), D1 (center) |

## Build & Flash

```sh
pio run -e adafruit_feather_esp32s3_reversetft -t upload
pio device monitor
```

## Run Host Tests

```sh
pio test -e native
```

## Pair with Claude Desktop

1. Claude → **Help → Troubleshooting → Enable Developer Mode**
2. Developer → **Open Hardware Buddy…**
3. Click **Connect**, select `Claude-XXXX` from the list

Once paired, the device reconnects automatically in the background on subsequent launches.

## Code Structure

```
src/
├── main.cpp          # Main loop: TFT rendering, BLE line accumulation
└── ble_bridge.cpp/h  # BLE init, NUS service, RX ring buffer, callbacks

lib/
├── protocol/         # JSON parser → ClaudeStatus struct
└── state/            # 4-state enum + derivation logic

test/
├── test_protocol/    # Parser unit tests
└── test_state/       # State machine unit tests
```
