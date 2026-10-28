#include <Arduino.h>
#include <NimBLEDevice.h>
#include "ble_remote.h"

static const char *SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *CHAR_RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *CHAR_TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

static NimBLECharacteristic *txChar = nullptr;
static BleLineCallback lineCb = nullptr;
static void *lineCbCtx = nullptr;
static volatile bool connected = false;

// Zeilen-Assembler im BLE-Callback
static char rxAssembleBuf[2048];
static size_t rxAssembleLen = 0;

// Thread-sichere Queue (4 Slots) für den Übergang NimBLE-Task -> Arduino loop()
static constexpr size_t QUEUE_SLOTS = 4;
static char cmdQueue[QUEUE_SLOTS][2048];
static volatile uint8_t qHead = 0;
static volatile uint8_t qTail = 0;
static portMUX_TYPE qMux = portMUX_INITIALIZER_UNLOCKED;

static void enqueueLine(const char *line) {
    portENTER_CRITICAL(&qMux);
    uint8_t nextHead = (qHead + 1) % QUEUE_SLOTS;
    if (nextHead != qTail) {
        strncpy(cmdQueue[qHead], line, sizeof(cmdQueue[0]) - 1);
        cmdQueue[qHead][sizeof(cmdQueue[0]) - 1] = 0;
        qHead = nextHead;
    }
    portEXIT_CRITICAL(&qMux);
}

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
        std::string v = c->getValue();
        for (char ch : v) {
            if (ch == '\n' || ch == '\r') {
                if (rxAssembleLen > 0) {
                    rxAssembleBuf[rxAssembleLen] = 0;
                    enqueueLine(rxAssembleBuf);
                }
                rxAssembleLen = 0;
            } else if (rxAssembleLen < sizeof(rxAssembleBuf) - 1) {
                rxAssembleBuf[rxAssembleLen++] = ch;
            }
        }
    }
};

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *s, NimBLEConnInfo &connInfo) override {
        connected = true;
    }
    void onDisconnect(NimBLEServer *s, NimBLEConnInfo &connInfo, int reason) override {
        connected = false;
        rxAssembleLen = 0;
        NimBLEDevice::startAdvertising();
    }
};

void bleBegin(uint32_t passkey, BleLineCallback onLine, void *ctx) {
    lineCb = onLine;
    lineCbCtx = ctx;

    NimBLEDevice::init("USB Receiver");
    NimBLEDevice::setMTU(517);
    NimBLEDevice::setSecurityAuth(true, true, true); // Bonding, MITM, Secure Connections
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    NimBLEDevice::setSecurityPasskey(passkey);

    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());
    server->advertiseOnDisconnect(true);

    NimBLEService *svc = server->createService(SERVICE_UUID);
    txChar = svc->createCharacteristic(CHAR_TX_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
    NimBLECharacteristic *rxChar = svc->createCharacteristic(
        CHAR_RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC
    );
    rxChar->setCallbacks(new RxCallbacks());
    svc->start();

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->enableScanResponse(true);
    adv->start();
}

void blePoll() {
    static char localLine[2048];
    while (true) {
        bool hasItem = false;
        portENTER_CRITICAL(&qMux);
        if (qTail != qHead) {
            strncpy(localLine, cmdQueue[qTail], sizeof(localLine) - 1);
            localLine[sizeof(localLine) - 1] = 0;
            qTail = (qTail + 1) % QUEUE_SLOTS;
            hasItem = true;
        }
        portEXIT_CRITICAL(&qMux);

        if (!hasItem) break;
        if (lineCb) {
            lineCb(localLine, lineCbCtx);
        }
    }
}

void bleSend(const char *msg) {
    if (!txChar || !connected || !msg) return;
    size_t len = strlen(msg);
    const size_t chunkMax = 180;
    size_t offset = 0;
    while (offset < len) {
        size_t n = (len - offset > chunkMax) ? chunkMax : (len - offset);
        txChar->setValue((const uint8_t *)(msg + offset), n);
        txChar->notify();
        offset += n;
        if (offset < len) delay(4);
    }
    // Zeilenumbruch anhängen, falls nicht vorhanden
    if (len == 0 || msg[len - 1] != '\n') {
        const char nl = '\n';
        txChar->setValue((const uint8_t *)&nl, 1);
        txChar->notify();
    }
}

void bleUpdatePasskey(uint32_t passkey) {
    NimBLEDevice::setSecurityPasskey(passkey);
}

bool bleConnected() {
    return connected;
}
