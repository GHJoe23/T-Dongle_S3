#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "storage.h"

static void sanitizeSettings(DeviceSettings &s) {
    if (s.blePin < 100000 || s.blePin > 999999) {
        s.blePin = DEFAULT_BLE_PIN;
    }
    if ((uint8_t)s.layout > 1) {
        s.layout = KeyboardLayout::DE_QWERTZ;
    }
    if (s.keyDelayMs < 2 || s.keyDelayMs > 500) {
        s.keyDelayMs = 12;
    }
    if (s.keyDelayMaxMs < s.keyDelayMs) {
        s.keyDelayMaxMs = s.keyDelayMs + 25;
    }
    if (s.keyDelayMaxMs > 1000) {
        s.keyDelayMaxMs = 1000;
    }
    if (s.jiggleMinSec < 2) {
        s.jiggleMinSec = 2;
    }
    if (s.jiggleMaxSec < s.jiggleMinSec) {
        s.jiggleMaxSec = s.jiggleMinSec;
    }
    if ((uint8_t)s.jiggleMode > 2) {
        s.jiggleMode = JiggleMode::PING_PONG;
    }
    if (s.micMode > 3) {
        s.micMode = 1;
    }
    if (s.micThreshold < 5 || s.micThreshold > 98) {
        s.micThreshold = 45;
    }
    if (s.micBossAction > 2) {
        s.micBossAction = 0;
    }
}

#if defined(ARDUINO_ARCH_NRF52)
// ============================================================================
// Seeed Studio XIAO nRF52840 Sense (InternalFS / LittleFS im QSPI/Flash)
// ============================================================================
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

static const char *FILE_CFG = "/cfg_v2.bin";
static const char *FILE_SNIP = "/snip_v2.bin";
static constexpr uint32_t STORAGE_MAGIC = 0x58494132UL; // "XIA2"

struct PersistedConfig {
    uint32_t magic;
    DeviceSettings settings;
};

struct PersistedSnippets {
    uint32_t magic;
    SnippetSlot slots[MAX_SNIPPETS];
};

static PersistedSnippets gSnippets;
static bool gFsReady = false;

static void saveSnippetsToFlash() {
    if (!gFsReady) return;
    InternalFS.remove(FILE_SNIP);
    File f(FILE_SNIP, FILE_O_WRITE, InternalFS);
    if (f) {
        gSnippets.magic = STORAGE_MAGIC;
        f.write((const uint8_t *)&gSnippets, sizeof(gSnippets));
        f.close();
    }
}

static void seedDefaultSnippets() {
    memset(&gSnippets, 0, sizeof(gSnippets));
    gSnippets.magic = STORAGE_MAGIC;

    gSnippets.slots[0].used = true;
    strncpy(gSnippets.slots[0].title, "Grussformel", sizeof(gSnippets.slots[0].title) - 1);
    strncpy(gSnippets.slots[0].text, "Mit freundlichen Gruessen\\n", sizeof(gSnippets.slots[0].text) - 1);

    gSnippets.slots[1].used = true;
    strncpy(gSnippets.slots[1].title, "Rueckruf", sizeof(gSnippets.slots[1].title) - 1);
    strncpy(gSnippets.slots[1].text, "Bitte um kurzen Rueckruf bei Gelegenheit. Danke!", sizeof(gSnippets.slots[1].text) - 1);

    gSnippets.slots[2].used = true;
    strncpy(gSnippets.slots[2].title, "Termin OK", sizeof(gSnippets.slots[2].title) - 1);
    strncpy(gSnippets.slots[2].text, "Vielen Dank fuer die Einladung, der Termin passt bei mir.", sizeof(gSnippets.slots[2].text) - 1);

    gSnippets.slots[3].used = true;
    strncpy(gSnippets.slots[3].title, "Login-Demo", sizeof(gSnippets.slots[3].title) - 1);
    strncpy(gSnippets.slots[3].text, "benutzer{TAB}{W:300}geheim\\n", sizeof(gSnippets.slots[3].text) - 1);

    saveSnippetsToFlash();
}

void storageBegin(DeviceSettings &outSettings) {
    outSettings = DeviceSettings{};
    gFsReady = InternalFS.begin();
    if (!gFsReady) {
        return;
    }

    bool cfgLoaded = false;
    if (InternalFS.exists(FILE_CFG)) {
        File f(FILE_CFG, FILE_O_READ, InternalFS);
        if (f) {
            PersistedConfig pc{};
            if (f.read((uint8_t *)&pc, sizeof(pc)) == (int)sizeof(pc) && pc.magic == STORAGE_MAGIC) {
                outSettings = pc.settings;
                sanitizeSettings(outSettings);
                cfgLoaded = true;
            }
            f.close();
        }
    }
    if (!cfgLoaded) {
        outSettings = DeviceSettings{};
        storageSaveSettings(outSettings);
    }

    bool snipLoaded = false;
    if (InternalFS.exists(FILE_SNIP)) {
        File f(FILE_SNIP, FILE_O_READ, InternalFS);
        if (f) {
            if (f.read((uint8_t *)&gSnippets, sizeof(gSnippets)) == (int)sizeof(gSnippets) &&
                gSnippets.magic == STORAGE_MAGIC) {
                snipLoaded = true;
            }
            f.close();
        }
    }
    if (!snipLoaded) {
        seedDefaultSnippets();
    }
}

void storageSaveSettings(const DeviceSettings &settings) {
    if (!gFsReady) return;
    PersistedConfig pc{};
    pc.magic = STORAGE_MAGIC;
    pc.settings = settings;
    sanitizeSettings(pc.settings);

    InternalFS.remove(FILE_CFG);
    File f(FILE_CFG, FILE_O_WRITE, InternalFS);
    if (f) {
        f.write((const uint8_t *)&pc, sizeof(pc));
        f.close();
    }
}

bool storageGetSnippet(uint8_t slot, SnippetSlot &outSlot) {
    memset(&outSlot, 0, sizeof(outSlot));
    if (slot >= MAX_SNIPPETS) return false;
    if (!gSnippets.slots[slot].used) return false;
    outSlot = gSnippets.slots[slot];
    return true;
}

bool storageSetSnippet(uint8_t slot, const char *title, const char *text) {
    if (slot >= MAX_SNIPPETS || !title || !text) return false;
    SnippetSlot &s = gSnippets.slots[slot];
    memset(&s, 0, sizeof(s));
    s.used = true;
    strncpy(s.title, title, sizeof(s.title) - 1);
    strncpy(s.text, text, sizeof(s.text) - 1);
    saveSnippetsToFlash();
    return true;
}

bool storageDeleteSnippet(uint8_t slot) {
    if (slot >= MAX_SNIPPETS) return false;
    memset(&gSnippets.slots[slot], 0, sizeof(SnippetSlot));
    saveSnippetsToFlash();
    return true;
}

#else
// ============================================================================
// LilyGO T-Dongle S3 (ESP32-S3 Preferences NVS)
// ============================================================================
#include <Preferences.h>

static Preferences prefs;
static const char *NS_CFG = "tdongle_cfg";
static const char *NS_SNIP = "tdongle_snp";
static constexpr uint8_t CFG_SCHEMA_VER = 2;

static void seedDefaultSnippets() {
    prefs.begin(NS_SNIP, false);
    if (!prefs.getBool("seeded", false)) {
        prefs.putBool("seeded", true);
        prefs.end();
        storageSetSnippet(0, "Grussformel", "Mit freundlichen Gruessen\\n");
        storageSetSnippet(1, "Rueckruf", "Bitte um kurzen Rueckruf bei Gelegenheit. Danke!");
        storageSetSnippet(2, "Termin OK", "Vielen Dank fuer die Einladung, der Termin passt bei mir.");
        storageSetSnippet(3, "Login-Demo", "benutzer{TAB}{W:300}geheim\\n");
    } else {
        prefs.end();
    }
}

void storageBegin(DeviceSettings &outSettings) {
    outSettings = DeviceSettings{};
    prefs.begin(NS_CFG, false);
    uint8_t ver = prefs.getUChar("ver", 0);
    if (ver != CFG_SCHEMA_VER) {
        prefs.putUChar("ver", CFG_SCHEMA_VER);
        prefs.putBool("stealth", false);
        prefs.putUChar("layout", 0);
    }

    outSettings.blePin = prefs.getUInt("pin", DEFAULT_BLE_PIN);
    uint8_t lay = prefs.getUChar("layout", 0);
    outSettings.layout = (lay == 1) ? KeyboardLayout::US_QWERTY : KeyboardLayout::DE_QWERTZ;
    outSettings.keyDelayMs = prefs.getUShort("delay", 12);
    outSettings.randomDelay = prefs.getBool("rnd_dly", false);
    outSettings.keyDelayMaxMs = prefs.getUShort("dly_max", 55);
    outSettings.jiggleMinSec = prefs.getUShort("jmin", 30);
    outSettings.jiggleMaxSec = prefs.getUShort("jmax", 60);
    uint8_t jm = prefs.getUChar("jmode", 0);
    outSettings.jiggleMode = (JiggleMode)(jm > 2 ? 0 : jm);
    outSettings.jiggleAutoStart = prefs.getBool("jauto", true);
    outSettings.stealthMode = prefs.getBool("stealth", false);
    outSettings.micMode = prefs.getUChar("mic_mode", 0);
    outSettings.micThreshold = prefs.getUChar("mic_thr", 45);
    outSettings.micBossAction = prefs.getUChar("mic_boss", 0);
    prefs.end();

    sanitizeSettings(outSettings);
    seedDefaultSnippets();
}

void storageSaveSettings(const DeviceSettings &settings) {
    prefs.begin(NS_CFG, false);
    prefs.putUChar("ver", CFG_SCHEMA_VER);
    prefs.putUInt("pin", settings.blePin);
    prefs.putUChar("layout", (uint8_t)settings.layout);
    prefs.putUShort("delay", settings.keyDelayMs);
    prefs.putBool("rnd_dly", settings.randomDelay);
    prefs.putUShort("dly_max", settings.keyDelayMaxMs);
    prefs.putUShort("jmin", settings.jiggleMinSec);
    prefs.putUShort("jmax", settings.jiggleMaxSec);
    prefs.putUChar("jmode", (uint8_t)settings.jiggleMode);
    prefs.putBool("jauto", settings.jiggleAutoStart);
    prefs.putBool("stealth", settings.stealthMode);
    prefs.putUChar("mic_mode", settings.micMode);
    prefs.putUChar("mic_thr", settings.micThreshold);
    prefs.putUChar("mic_boss", settings.micBossAction);
    prefs.end();
}

bool storageGetSnippet(uint8_t slot, SnippetSlot &outSlot) {
    memset(&outSlot, 0, sizeof(outSlot));
    if (slot >= MAX_SNIPPETS) return false;

    char kUsed[12], kTitle[12], kText[12];
    snprintf(kUsed, sizeof(kUsed), "u%u", slot);
    snprintf(kTitle, sizeof(kTitle), "t%u", slot);
    snprintf(kText, sizeof(kText), "x%u", slot);

    prefs.begin(NS_SNIP, true);
    outSlot.used = prefs.getBool(kUsed, false);
    if (outSlot.used) {
        prefs.getString(kTitle, outSlot.title, sizeof(outSlot.title));
        prefs.getString(kText, outSlot.text, sizeof(outSlot.text));
    }
    prefs.end();
    return outSlot.used;
}

bool storageSetSnippet(uint8_t slot, const char *title, const char *text) {
    if (slot >= MAX_SNIPPETS || !title || !text) return false;

    char kUsed[12], kTitle[12], kText[12];
    snprintf(kUsed, sizeof(kUsed), "u%u", slot);
    snprintf(kTitle, sizeof(kTitle), "t%u", slot);
    snprintf(kText, sizeof(kText), "x%u", slot);

    char safeTitle[32] = {0};
    char safeText[256] = {0};
    strncpy(safeTitle, title, sizeof(safeTitle) - 1);
    strncpy(safeText, text, sizeof(safeText) - 1);

    prefs.begin(NS_SNIP, false);
    prefs.putBool(kUsed, true);
    prefs.putString(kTitle, safeTitle);
    prefs.putString(kText, safeText);
    prefs.end();
    return true;
}

bool storageDeleteSnippet(uint8_t slot) {
    if (slot >= MAX_SNIPPETS) return false;
    char kUsed[12], kTitle[12], kText[12];
    snprintf(kUsed, sizeof(kUsed), "u%u", slot);
    snprintf(kTitle, sizeof(kTitle), "t%u", slot);
    snprintf(kText, sizeof(kText), "x%u", slot);

    prefs.begin(NS_SNIP, false);
    prefs.putBool(kUsed, false);
    prefs.remove(kTitle);
    prefs.remove(kText);
    prefs.end();
    return true;
}
#endif
