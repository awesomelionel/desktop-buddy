#include "ble_bridge.h"
#include <Arduino.h>
#include <NimBLEDevice.h>

// Nordic UART Service UUIDs — every BLE serial example uses these.
#define NUS_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// 4KB matches the desktop's max event payload (REFERENCE.md). Under burst
// the ring needs to hold one full line in flight while loop() drains it.
static const size_t RX_CAP = 4096;
static uint8_t  rxBuf[RX_CAP];
static volatile size_t rxHead = 0;
static volatile size_t rxTail = 0;

static NimBLEServer*         server = nullptr;
static NimBLECharacteristic* rxChar = nullptr;
static NimBLECharacteristic* txChar = nullptr;
static volatile bool         connected = false;

static void rxPush(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        size_t next = (rxHead + 1) % RX_CAP;
        if (next == rxTail) return;  // full — drop
        rxBuf[rxHead] = p[i];
        rxHead = next;
    }
}

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        std::string v = c->getValue();
        if (v.length() > 0)
            rxPush(reinterpret_cast<const uint8_t*>(v.data()), v.length());
    }
};

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*) override {
        connected = true;
        Serial.println("[ble] connected");
    }
    void onDisconnect(NimBLEServer*) override {
        connected = false;
        Serial.println("[ble] disconnected, restarting advertising");
        NimBLEDevice::startAdvertising();
    }
};

void ble_init(const char* device_name) {
    NimBLEDevice::init(device_name);
    NimBLEDevice::setMTU(517);

    server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    NimBLEService* svc = server->createService(NUS_SERVICE_UUID);

    txChar = svc->createCharacteristic(NUS_TX_UUID, NIMBLE_PROPERTY::NOTIFY);

    rxChar = svc->createCharacteristic(
        NUS_RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rxChar->setCallbacks(new RxCallbacks());

    svc->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->setMaxPreferred(0x12);
    NimBLEDevice::startAdvertising();
    Serial.printf("[ble] advertising as '%s'\n", device_name);
}

bool   ble_connected() { return connected; }
size_t ble_available() { return (rxHead + RX_CAP - rxTail) % RX_CAP; }
int    ble_read() {
    if (rxHead == rxTail) return -1;
    int b = rxBuf[rxTail];
    rxTail = (rxTail + 1) % RX_CAP;
    return b;
}

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
