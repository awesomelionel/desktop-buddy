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

// Update the advertising name without tearing down the GATT services.
// The GAP device name is updated and advertising is restarted, which
// disconnects any connected central briefly. No-op if name is null/empty.
void   ble_set_device_name(const char* device_name);

bool   ble_connected();
size_t ble_available();
int    ble_read();   // -1 if empty

// Push `line` followed by '\n' out the TX characteristic via notify.
// Returns false if not connected. Best-effort: if the notify queue is
// full, returns false; the caller should drop, not retry.
bool   ble_write_line(const char* line);
