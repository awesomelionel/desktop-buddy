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
// TX char: 6e400003-b5a3-f393-e0a9-e50e24dcca9e (notify, unused in v1)

void   ble_init(const char* device_name);
bool   ble_connected();
size_t ble_available();
int    ble_read();   // -1 if empty
