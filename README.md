# claude-buddy

A minimal ESP32-S3 firmware (Adafruit Reverse TFT Feather) that connects to
Claude Desktop over BLE (Nordic UART Service) and shows the current session
state on the TFT as text.

See `cdb/REFERENCE.md` for the wire protocol; this implementation is a
ground-up rewrite, not a fork of `cdb/`.

## Build & flash

    pio run -e adafruit_feather_esp32s3_reversetft -t upload
    pio device monitor

## Run host tests

    pio test -e native

## Pair with Claude Desktop

1. Claude → Help → Troubleshooting → Enable Developer Mode
2. Developer → Open Hardware Buddy…
3. Click Connect, pick `Claude-XXXX` from the list
