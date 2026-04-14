#include "door.h"
#include "motor.h"
#include "light.h"
#include "logger.h"
#include "storage.h"
#include "system.h"
#include "logic.h"
#include "pins.h"

// ==================================================
// GLOBALE VARIABLEN (dieses Moduls)
// ==================================================
bool         doorOpen         = false;
DoorPhase    doorPhase        = PHASE_IDLE;
unsigned long doorOpenedAt    = 0;

bool         learningActive   = false;
bool         learningOpenDone = false;
unsigned long learnStartTime  = 0;
bool         openedByLight    = false;
unsigned long manualOverrideUntil = 0;

bool          preLightOpenDone  = false;
bool          preLightCloseDone = false;
unsigned long lightAboveSince   = 0;
unsigned long lightBelowSince   = 0;
unsigned long plannedCloseAt    = 0;

// ==================================================
// TASTER KLAPPE
// ==================================================
void updateButton()
{
    static int lastRaw       = HIGH;
    static int stable        = HIGH;
    static unsigned long lastChange  = 0;
    static unsigned long pressStart  = 0;
    const unsigned long DEBOUNCE_MS  = 50;   // war 30ms
    const unsigned long HOLD_MIN_MS  = 150;  // Mindest-Haltezeit – filtert Störimpulse

    extern unsigned long bootTime;
    int raw = digitalRead(BUTTON_PIN);
    if (raw != lastRaw) { lastChange = millis(); lastRaw = raw; }
    if ((millis() - lastChange) > DEBOUNCE_MS && raw != stable)
    {
        stable = raw;
        if (stable == LOW && millis() - bootTime > 2000) {
            pressStart = millis();  // Druckbeginn merken
        }
        if (stable == HIGH && pressStart > 0) {
            // Nur auslösen wenn Taste lang genug gehalten wurde
            if (millis() - pressStart >= HOLD_MIN_MS)
                handleButtonPress();
            pressStart = 0;
        }
    }
}

// ==================================================
// TASTER STALLLICHT
// ==================================================
void updateStallButton()
{
    static int lastRaw    = HIGH;
    static int stable     = HIGH;
    static unsigned long lastChange = 0;
    const unsigned long DEBOUNCE_MS = 30;

    extern unsigned long bootTime;
    int raw = digitalRead(STALL_BUTTON_PIN);
    if (raw != lastRaw) { lastChange = millis(); lastRaw = raw; }
    if ((millis() - lastChange) > DEBOUNCE_MS && raw != stable)
    {
        stable = raw;
        if (stable == LOW && millis() - bootTime > 3000)
        {
            if (stallLightActive) stallLightOff();
            else stallLightOn();
        }
    }
}

// ==================================================
// TASTER-AKTION
// ==================================================
void handleButtonPress()
{
    // ===== EINLERNEN =====
    if (learningActive)
    {
        if (!learningOpenDone)
        {
            openPosition = millis() - learnStartTime;
            saveMotorPositions();
            learningOpenDone = true;
            addLog("Open-Position gespeichert: " + String(openPosition));
            learnStartTime = millis();
            // Motor schließen mit langem Timeout (30s)
            motorState = MOTOR_CLOSING;
            motorClose();
            motorUntil = millis() + 30000UL;
            addLog("Fahre Richtung CLOSE – Taster drücken wenn geschlossen");
        }
        else
        {
            closePosition = millis() - learnStartTime;
            saveMotorPositions();
            learningActive = false;
            motorStop();
            motorState = MOTOR_STOPPED;
            doorOpen = false;
            saveDoorState();
            addLog("Close-Position gespeichert: " + String(closePosition));
            addLog("Einlernen abgeschlossen");
        }
        return;
    }

    // ===== OTA/SAFESTATE =====
    if (otaInProgress || ioSafeState)
    {
        addLog("Taster ignoriert (OTA/Safe-State)");
        return;
    }

    // ===== MOTOR STOPP =====
    if (motorState != MOTOR_STOPPED)
    {
        motorStop();
        motorState  = MOTOR_STOPPED;
        motorReason = "Stop/Taster";
        doorPhase   = doorOpen ? PHASE_OPEN : PHASE_IDLE;
        addLog("Motor per Taster gestoppt");
        return;
    }

    // ===== TOGGLE =====
    if (doorOpen)
    {
        doorPhase   = PHASE_CLOSING;
        motorReason = "manuell/Taster";
        startMotorClose(closePosition);
        actionLock          = true;
        preLightOpenDone    = false;
        manualOverrideUntil = millis() + 300000UL;
        addLog("Schließvorgang gestartet (Taster)");
    }
    else
    {
        doorPhase   = PHASE_OPENING;
        motorReason = "manuell/Taster";
        startMotorOpen(openPosition);
        actionLock          = true;
        preLightCloseDone   = false;
        preLightOpenDone    = false;
        manualOverrideUntil = millis() + 300000UL;  // 5 Minuten Override
        lightBelowSince     = 0;
        nightLock           = false;
        addLog("Öffnung gestartet (Taster) – Automatik pausiert 5 min");
    }
}

// ==================================================
// TASTER ROTLICHT  –  Kurz: Locklicht  |  Lang: Rotlicht
// ==================================================
void updateRedButton()
{
    static int           lastRaw       = HIGH;
    static int           stable        = HIGH;
    static unsigned long lastChange    = 0;
    static unsigned long pressedAt     = 0;
    static bool          longFired     = false;

    const unsigned long  DEBOUNCE_MS   = 30;
    const unsigned long  LONG_PRESS_MS = 700;

    extern unsigned long bootTime;
    int raw = digitalRead(RED_BUTTON_PIN);

    if (raw != lastRaw) { lastChange = millis(); lastRaw = raw; }

    if ((millis() - lastChange) > DEBOUNCE_MS)
    {
        if (raw != stable)
        {
            // --- Flankenwechsel ---
            stable = raw;
            if (stable == LOW && millis() - bootTime > 3000)
            {
                // Taste gedrueckt: Zeitstempel merken
                pressedAt = millis();
                longFired = false;
            }
            else if (stable == HIGH && !longFired)
            {
                // Kurzdruck: Taste losgelassen bevor Langdruck ausgeloest
                if (rgbRedActive) {
                    rgbRedOff();                          // Rotlicht aus
                } else if (manualLightActive) {
                    manualLightActive = false;            // Locklicht aus
                    lightOff();
                    lightActive = false;
                } else {
                    manualLightActive = true;             // Locklicht ein
                    lightOn();
                    lightActive = true;
                }
            }
        }
        else if (stable == LOW && !longFired &&
                 millis() - bootTime > 3000 &&
                 millis() - pressedAt >= LONG_PRESS_MS)
        {
            // Langdruck: Taste laenger als 700ms gehalten
            longFired = true;
            if (!stallLightActive && !rgbRedActive)
                rgbRedOn();
        }
    }
}
