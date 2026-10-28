#include <Arduino.h>
#include <string.h>
#include "ble_remote.h"

// Gemeinsamer lock-freier SPSC Byte-Ringpuffer (Single-Producer: BLE RX-Callback, Single-Consumer: blePoll)
static constexpr uint16_t RX_RING_SIZE = 2048; // Zweierpotenz
static constexpr uint16_t RX_RING_MASK = RX_RING_SIZE - 1;
static uint8_t rxRing[RX_RING_SIZE];
static volatile uint16_t rxHead = 0;
static volatile uint16_t rxTail = 0;

static char rxAssembleBuf[1024];
static size_t rxAssembleLen = 0;

static BleLineCallback lineCb = nullptr;
static void *lineCbCtx = nullptr;
static volatile bool connected = false;

static inline void rxMemoryBarrier() {
#if defined(ARDUINO_ARCH_NRF52)
    __DMB();
#else
    __sync_synchronize();
#endif
}

static void pushRxBytes(const uint8_t *data, size_t len) {
    uint16_t head = rxHead;
    uint16_t tail = rxTail;
    for (size_t i = 0; i < len; i++) {
        uint16_t next = (head + 1) & RX_RING_MASK;
        if (next == tail) {
            break; // Ringpuffer voll
        }
        rxRing[head] = data[i];
        head = next;
    }
    rxMemoryBarrier();
    rxHead = head;
}

void blePoll() {
    uint16_t tail = rxTail;
    uint16_t head = rxHead;
    rxMemoryBarrier();

    while (tail != head) {
        char ch = (char)rxRing[tail];
        tail = (tail + 1) & RX_RING_MASK;

        if (ch == '\n' || ch == '\r') {
            if (rxAssembleLen > 0) {
                rxAssembleBuf[rxAssembleLen] = 0;
                rxAssembleLen = 0;
                rxMemoryBarrier();
                rxTail = tail;
                if (lineCb) {
                    lineCb(rxAssembleBuf, lineCbCtx);
                }
                head = rxHead;
                rxMemoryBarrier();
            }
        } else if (rxAssembleLen < sizeof(rxAssembleBuf) - 1) {
            rxAssembleBuf[rxAssembleLen++] = ch;
        }
    }

    rxMemoryBarrier();
    rxTail = tail;
}

void bleUpdatePasskey(uint32_t passkey) {
    (void)passkey;
}

bool bleConnected() {
    return connected;
}

#if defined(ARDUINO_ARCH_NRF52)
// ============================================================================
// Seeed Studio XIAO nRF52840 Sense (Nordic SoftDevice S140 / Adafruit Bluefruit)
// ============================================================================
#include <bluefruit.h>

static BLEService nusService(BLEUART_UUID_SERVICE);
static BLECharacteristic txChar(BLEUART_UUID_CHR_TXD);
static BLECharacteristic rxChar(BLEUART_UUID_CHR_RXD);
static volatile uint16_t activeConnHandle = BLE_CONN_HANDLE_INVALID;

static constexpr uint16_t BLE_ATTR_MAX_LEN = 244; // Passend zu SoftDevice S140 Max-MTU (247 - 3)
static char latestStatusBuf[BLE_ATTR_MAX_LEN + 1] = "STAT {\"j\":1}\n";

static void onRxWrite(uint16_t conn_hdl, BLECharacteristic *chr, uint8_t *data, uint16_t len) {
    (void)conn_hdl;
    (void)chr;
    pushRxBytes(data, len);
}

static void onBleConnect(uint16_t conn_handle) {
    activeConnHandle = conn_handle;
    connected = true;

    BLEConnection *conn = Bluefruit.Connection(conn_handle);
    if (conn) {
        conn->requestMtuExchange(247);
    }
}

static void onBleDisconnect(uint16_t conn_handle, uint8_t reason) {
    (void)conn_handle;
    (void)reason;
    connected = false;
    activeConnHandle = BLE_CONN_HANDLE_INVALID;
    rxTail = rxHead;
    rxAssembleLen = 0;
}

void bleBegin(uint32_t passkey, BleLineCallback onLine, void *ctx) {
    (void)passkey;
    lineCb = onLine;
    lineCbCtx = ctx;

    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
    Bluefruit.begin();
    Bluefruit.setTxPower(4);
    Bluefruit.setName("USB Receiver");
    Bluefruit.autoConnLed(false);

    Bluefruit.Periph.setConnInterval(6, 12);
    Bluefruit.Periph.setConnectCallback(onBleConnect);
    Bluefruit.Periph.setDisconnectCallback(onBleDisconnect);

    nusService.begin();

    txChar.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
    txChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    txChar.setMaxLen(BLE_ATTR_MAX_LEN);
    txChar.begin();
    txChar.write((const uint8_t *)latestStatusBuf, (uint16_t)strlen(latestStatusBuf));

    rxChar.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
    rxChar.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
    rxChar.setMaxLen(BLE_ATTR_MAX_LEN);
    rxChar.setWriteCallback(onRxWrite);
    rxChar.begin();

    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(nusService);
    Bluefruit.ScanResponse.addName();

    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);
    Bluefruit.Advertising.setFastTimeout(30);
    Bluefruit.Advertising.start(0);
}

void bleSetStatus(const char *statusJsonLine) {
    if (!statusJsonLine) return;
    snprintf(latestStatusBuf, sizeof(latestStatusBuf), "%s\n", statusJsonLine);
    uint16_t len = (uint16_t)strlen(latestStatusBuf);

    if (connected && activeConnHandle != BLE_CONN_HANDLE_INVALID && txChar.notifyEnabled()) {
        BLEConnection *conn = Bluefruit.Connection(activeConnHandle);
        if (conn && conn->getMtu() >= (uint16_t)(len + 3)) {
            txChar.notify((const uint8_t *)latestStatusBuf, len);
        }
    }
    txChar.write((const uint8_t *)latestStatusBuf, len);
}

void bleSend(const char *msg) {
    if (!msg) return;
    if (strncmp(msg, "STAT ", 5) == 0) {
        bleSetStatus(msg);
        return;
    }
    static char outBuf[BLE_ATTR_MAX_LEN + 1];
    snprintf(outBuf, sizeof(outBuf), "%s\n", msg);
    uint16_t len = (uint16_t)strlen(outBuf);
    if (connected && txChar.notifyEnabled()) {
        txChar.notify((const uint8_t *)outBuf, len);
    }
    txChar.write((const uint8_t *)latestStatusBuf, (uint16_t)strlen(latestStatusBuf));
}

#else
// ============================================================================
// LilyGO T-Dongle S3 (ESP32-S3 / NimBLE-Arduino)
// ============================================================================
#include <NimBLEDevice.h>

static const char *SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *CHAR_RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *CHAR_TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

static NimBLECharacteristic *txChar = nullptr;
static char latestStatusBuf[320] = "STAT {\"j\":1}\n";

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
        (void)connInfo;
        std::string v = c->getValue();
        if (!v.empty()) {
            pushRxBytes((const uint8_t *)v.data(), v.size());
        }
    }
};

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *s, NimBLEConnInfo &connInfo) override {
        (void)s;
        (void)connInfo;
        connected = true;
    }
    void onDisconnect(NimBLEServer *s, NimBLEConnInfo &connInfo, int reason) override {
        (void)s;
        (void)connInfo;
        (void)reason;
        connected = false;
        rxTail = rxHead;
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
    txChar->setValue((const uint8_t *)latestStatusBuf, strlen(latestStatusBuf));
}
#endif
