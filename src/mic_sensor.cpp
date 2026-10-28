#include <Arduino.h>
#include "mic_sensor.h"

#if defined(ARDUINO_ARCH_NRF52)
// ============================================================================
// Seeed Studio XIAO nRF52840 Sense: Onboard PDM-Digitalmikrofon (MSM261D3526H1CPM)
// ============================================================================
#include <PDM.h>

static MicMode gMode = MicMode::CLAP_CONTROL;
static uint8_t gThreshold = 45;
static bool gPdmRunning = false;

static int16_t gSampleBuf[256];
static volatile uint16_t gLatestPeakRaw = 0;
static volatile bool gNewSamples = false;

static uint8_t gDisplayLevel = 0;
static uint32_t gLastDecayMs = 0;
static uint32_t gTriggerCount = 0;

static bool gInImpulse = false;
static uint32_t gImpulseStartMs = 0;
static uint32_t gLastClapMs = 0;
static uint8_t gClapCount = 0;

static uint32_t gBossArmedAtMs = 0;
static uint8_t gBossHitFrames = 0;

static void onPDMdata() {
    int bytesAvailable = PDM.available();
    if (bytesAvailable <= 0) return;
    if (bytesAvailable > (int)sizeof(gSampleBuf)) {
        bytesAvailable = (int)sizeof(gSampleBuf);
    }

    int bytesRead = PDM.read(gSampleBuf, bytesAvailable);
    int samples = bytesRead / 2;
    if (samples <= 0) return;

    int32_t sum = 0;
    for (int i = 0; i < samples; i++) {
        sum += gSampleBuf[i];
    }
    int32_t mean = sum / samples;

    uint16_t peak = 0;
    for (int i = 0; i < samples; i++) {
        int32_t diff = (int32_t)gSampleBuf[i] - mean;
        if (diff < 0) diff = -diff;
        if ((uint16_t)diff > peak) {
            peak = (uint16_t)diff;
        }
    }

    if (peak > gLatestPeakRaw) {
        gLatestPeakRaw = peak;
    }
    gNewSamples = true;
}

static void startPdmIfNeeded() {
    if (gPdmRunning) return;
    PDM.onReceive(onPDMdata);
    PDM.setGain(24);
    if (PDM.begin(1, 16000)) {
        gPdmRunning = true;
    }
}

static void stopPdmIfNeeded() {
    if (!gPdmRunning) return;
    PDM.end();
    gPdmRunning = false;
    gDisplayLevel = 0;
    gLatestPeakRaw = 0;
}

void micBegin(MicMode mode, uint8_t threshold) {
    PDM.setBufferSize(512);
    micConfigure(mode, threshold);
}

void micConfigure(MicMode mode, uint8_t threshold) {
    if (threshold < 5) threshold = 5;
    if (threshold > 98) threshold = 98;
    gMode = mode;
    gThreshold = threshold;
    gInImpulse = false;
    gClapCount = 0;
    gBossHitFrames = 0;

    if (gMode == MicMode::OFF) {
        stopPdmIfNeeded();
    } else {
        startPdmIfNeeded();
        if (gMode == MicMode::BOSS_GUARD) {
            gBossArmedAtMs = millis() + 3000UL;
        }
    }
}

MicEvent micPoll(uint32_t now) {
    if (gMode == MicMode::OFF || !gPdmRunning) {
        return MicEvent::NONE;
    }

    if (now - gLastDecayMs >= 80) {
        gLastDecayMs = now;
        if (gDisplayLevel > 3) gDisplayLevel -= 3;
        else gDisplayLevel = 0;
    }

    if (gNewSamples) {
        gNewSamples = false;
        uint16_t raw = gLatestPeakRaw;
        gLatestPeakRaw = 0;

        uint32_t scaled = ((uint32_t)raw * 100UL) / 12000UL;
        if (scaled > 100UL) scaled = 100UL;
        uint8_t currentLevel = (uint8_t)scaled;

        if (currentLevel > gDisplayLevel) {
            gDisplayLevel = currentLevel;
        }

        if (gMode == MicMode::BOSS_GUARD) {
            if ((int32_t)(now - gBossArmedAtMs) >= 0) {
                if (currentLevel >= gThreshold) {
                    gBossHitFrames++;
                    if (gBossHitFrames >= 2) {
                        gBossHitFrames = 0;
                        gBossArmedAtMs = now + 5000UL;
                        gTriggerCount++;
                        return MicEvent::BOSS_TRIGGER;
                    }
                } else {
                    gBossHitFrames = 0;
                }
            }
            return MicEvent::NONE;
        }

        uint8_t lowHysteresis = (gThreshold > 12) ? (gThreshold - 10) : (gThreshold / 2);

        if (!gInImpulse && currentLevel >= gThreshold) {
            gInImpulse = true;
            gImpulseStartMs = now;
        } else if (gInImpulse && currentLevel <= lowHysteresis) {
            uint32_t duration = now - gImpulseStartMs;
            gInImpulse = false;

            if (duration >= 5 && duration <= 240) {
                if (gClapCount == 0 || (now - gLastClapMs >= 110 && now - gLastClapMs <= 650)) {
                    gClapCount++;
                    gLastClapMs = now;
                } else if (now - gLastClapMs > 650) {
                    gClapCount = 1;
                    gLastClapMs = now;
                }
            }
        } else if (gInImpulse && (now - gImpulseStartMs > 300)) {
            gClapCount = 0;
        }
    }

    if (gClapCount > 0 && !gInImpulse && (now - gLastClapMs >= 680)) {
        uint8_t count = gClapCount;
        gClapCount = 0;
        if (count == 2) {
            gTriggerCount++;
            return MicEvent::DOUBLE_CLAP;
        }
        if (count >= 3) {
            gTriggerCount++;
            return MicEvent::TRIPLE_CLAP;
        }
    }

    return MicEvent::NONE;
}

uint8_t micGetLevel() { return gDisplayLevel; }
bool micIsRunning() { return gPdmRunning; }
uint32_t micGetTriggerCount() { return gTriggerCount; }

#else
// ============================================================================
// LilyGO T-Dongle S3: Kein Onboard-Mikrofon (No-Op Stubs)
// ============================================================================
void micBegin(MicMode, uint8_t) {}
void micConfigure(MicMode, uint8_t) {}
MicEvent micPoll(uint32_t) { return MicEvent::NONE; }
uint8_t micGetLevel() { return 0; }
bool micIsRunning() { return false; }
uint32_t micGetTriggerCount() { return 0; }
#endif
