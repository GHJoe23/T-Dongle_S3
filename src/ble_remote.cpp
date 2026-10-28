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

static char latestStatusBuf[320] = "STAT {\"j\":1}\n";

// Zeilen-Assembler im BLE-Callback
static char rxAssembleBuf[2048];
static size_t rxAssembleLen = 0;

// Thread-sichere Queue (8 Slots) für den Übergang NimBLE-Task -> Arduino loop()
static constexpr size_t QUEUE_SLOTS = 8;
static char cmdQueue[QUEUE_SLOTS][1024];
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
    (void)passkey;
    lineCb = onLine;
    lineCbCtx = ctx;

    NimBLEDevice::init("USB Receiver");
    uint8_t cleanAddr[6] = {0x40, 0x04, 0x00, 0x7A, 0x05, 0xDF};
    NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
    NimBLEDevice::setOwnAddr(cleanAddr);

    NimBLEDevice::setMTU(517);
    NimBLEDevice::setSecurityAuth(false, false, false);

    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());
    server->advertiseOnDisconnect(true);

    NimBLEService *svc = server->createService(SERVICE_UUID);
    txChar = svc->createCharacteristic(CHAR_TX_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
    txChar->setValue((const uint8_t *)latestStatusBuf, strlen(latestStatusBuf));

    NimBLECharacteristic *rxChar = svc->createCharacteristic(
        CHAR_RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    rxChar->setCallbacks(new RxCallbacks());

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->enableScanResponse(true);
    adv->start();
}

void blePoll() {
    static char localLine[1024];
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

void bleSetStatus(const char *statusJsonLine) {
    if (!statusJsonLine) return;
    snprintf(latestStatusBuf, sizeof(latestStatusBuf), "%s\n", statusJsonLine);
    if (txChar) {
        txChar->setValue((const uint8_t *)latestStatusBuf, strlen(latestStatusBuf));
        if (connected) {
            txChar->notify();
        }
    }
}

void bleSend(const char *msg) {
    if (!txChar || !msg) return;
    if (strncmp(msg, "STAT ", 5) == 0) {
        bleSetStatus(msg);
        return;
    }
    char outBuf[360];
    snprintf(outBuf, sizeof(outBuf), "%s\n", msg);
    if (connected) {
        txChar->setValue((const uint8_t *)outBuf, strlen(outBuf));
        txChar->notify();
        delay(8);
    }
    // Danach wieder den aktuellen STAT-String als Read-Wert hinterlegen,
    // damit txChar.readValue() im Browser jederzeit den vollständigen Status liefert.
    txChar->setValue((const uint8_t *)latestStatusBuf, strlen(latestStatusBuf));
}

void bleUpdatePasskey(uint32_t passkey) {
    (void)passkey;
}

bool bleConnected() {
    return connected;
}
