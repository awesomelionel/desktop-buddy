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
