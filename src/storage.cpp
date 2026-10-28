#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>
#include <string.h>
#include "storage.h"

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
    prefs.begin(NS_CFG, false);
    uint8_t ver = prefs.getUChar("ver", 0);
    if (ver != CFG_SCHEMA_VER) {
        // Einmalige Migration auf Schema v2: Display wieder aktivieren & DE-Layout als Startwert
        prefs.putUChar("ver", CFG_SCHEMA_VER);
        prefs.putBool("stealth", false);
        prefs.putUChar("layout", 0);
    }

    outSettings.blePin = prefs.getUInt("pin", DEFAULT_BLE_PIN);
    if (outSettings.blePin < 100000 || outSettings.blePin > 999999) {
        outSettings.blePin = DEFAULT_BLE_PIN;
    }
    uint8_t lay = prefs.getUChar("layout", 0);
    outSettings.layout = (lay == 1) ? KeyboardLayout::US_QWERTY : KeyboardLayout::DE_QWERTZ;
    outSettings.keyDelayMs = prefs.getUShort("delay", 12);
    if (outSettings.keyDelayMs < 2 || outSettings.keyDelayMs > 500) {
        outSettings.keyDelayMs = 12;
    }
    outSettings.randomDelay = prefs.getBool("rnd_dly", false);
    outSettings.keyDelayMaxMs = prefs.getUShort("dly_max", 55);
    if (outSettings.keyDelayMaxMs < outSettings.keyDelayMs) {
        outSettings.keyDelayMaxMs = outSettings.keyDelayMs + 25;
    }
    if (outSettings.keyDelayMaxMs > 1000) {
        outSettings.keyDelayMaxMs = 1000;
    }

    outSettings.jiggleMinSec = prefs.getUShort("jmin", 30);
    outSettings.jiggleMaxSec = prefs.getUShort("jmax", 60);
    if (outSettings.jiggleMinSec < 2) outSettings.jiggleMinSec = 2;
    if (outSettings.jiggleMaxSec < outSettings.jiggleMinSec) {
        outSettings.jiggleMaxSec = outSettings.jiggleMinSec;
    }
    uint8_t jm = prefs.getUChar("jmode", 0);
    if (jm > 2) jm = 0;
    outSettings.jiggleMode = (JiggleMode)jm;
    outSettings.jiggleAutoStart = prefs.getBool("jauto", true);
    outSettings.stealthMode = prefs.getBool("stealth", false);
    prefs.end();

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
