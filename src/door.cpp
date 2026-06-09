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

// Klappe 2 Zustand
bool         door2Open        = false;
DoorPhase    door2Phase       = PHASE_IDLE;

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
// TASTER KLAPPE 1  –  Kurz: Klappe 1 toggeln  |  Lang (≥700ms): Klappe 2 toggeln
// ==================================================
// Zwei Aktionen auf einem Taster, gleiches Muster wie updateRedButton().
// Im Lernmodus ist die Lang-Aktion deaktiviert, damit das Einlernen nicht
// versehentlich Klappe 2 toggelt. Klappe 2 ist zusätzlich über BUTTON2_PIN
// (siehe updateButton2) erreichbar – beide Wege rufen handleButton2Press().
void updateButton()
{
    static int           lastRaw       = HIGH;
    static int           stable        = HIGH;
    static unsigned long lastChange    = 0;
    static unsigned long pressedAt     = 0;
    static bool          longFired     = false;

    const unsigned long  DEBOUNCE_MS   = 50;
    const unsigned long  HOLD_MIN_MS   = 150;   // Mindest-Haltezeit Kurzdruck (Anti-Glitch)
    const unsigned long  LONG_PRESS_MS = 700;   // ab hier zählt es als Langdruck

    extern unsigned long bootTime;
    int raw = digitalRead(BUTTON_PIN);
    if (raw != lastRaw) { lastChange = millis(); lastRaw = raw; }

    if ((millis() - lastChange) > DEBOUNCE_MS)
    {
        if (raw != stable)
        {
            // ----- Flankenwechsel -----
            stable = raw;

            if (stable == LOW && millis() - bootTime > 2000)
            {
                // Taste gedrückt
                pressedAt = millis();
                longFired = false;
            }
            else if (stable == HIGH && pressedAt > 0)
            {
                // Taste losgelassen
                if (!longFired && (millis() - pressedAt >= HOLD_MIN_MS))
                {
                    // Kurzdruck → Klappe 1 toggeln (oder Lernschritt)
                    handleButtonPress();
                }
                pressedAt = 0;
            }
        }
        else if (stable == LOW && !longFired && pressedAt > 0 &&
                 !learningActive &&                          // Lernmodus: KEIN Langdruck
                 (millis() - pressedAt >= LONG_PRESS_MS))
        {
            // Langdruck erreicht (feuert SOFORT, ohne auf Loslassen zu warten)
            // → Klappe 2 toggeln
            longFired = true;
            handleButton2Press();
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
// TASTER KLAPPE 2  (dedizierter Taster auf BUTTON2_PIN)
// ==================================================
// Einfacher Kurzdruck-Toggle, keine Lang-Funktion. Ruft denselben
// Action-Handler wie der Langdruck auf dem Klappe-1-Taster.
void updateButton2()
{
    static int           lastRaw     = HIGH;
    static int           stable      = HIGH;
    static unsigned long lastChange  = 0;
    static unsigned long pressStart  = 0;
    const unsigned long  DEBOUNCE_MS = 50;
    const unsigned long  HOLD_MIN_MS = 150;

    extern unsigned long bootTime;
    int raw = digitalRead(BUTTON2_PIN);
    if (raw != lastRaw) { lastChange = millis(); lastRaw = raw; }
    if ((millis() - lastChange) > DEBOUNCE_MS && raw != stable)
    {
        stable = raw;
        if (stable == LOW && millis() - bootTime > 2000) {
            pressStart = millis();
        }
        if (stable == HIGH && pressStart > 0) {
            if (millis() - pressStart >= HOLD_MIN_MS)
                handleButton2Press();
            pressStart = 0;
        }
    }
}

// ==================================================
// TASTER-AKTION KLAPPE 2
// ==================================================
// Gemeinsam genutzt von:
//   • Langdruck (≥700ms) auf BUTTON_PIN  (updateButton)
//   • Kurzdruck auf BUTTON2_PIN          (updateButton2)
// Spiegelt /door2 (Web-Handler in main.cpp).
void handleButton2Press()
{
    // ===== OTA / SAFE-STATE =====
    if (otaInProgress || ioSafeState)
    {
        addLog("Klappe2 Taster ignoriert (OTA/Safe-State)");
        return;
    }

    // ===== MOTOR STOPP =====
    if (motor2State != MOTOR_STOPPED)
    {
        motor2Stop();
        motor2State  = MOTOR_STOPPED;
        motor2Reason = "Stop/Taster";
        door2Phase   = door2Open ? PHASE_OPEN : PHASE_IDLE;
        addLog("Klappe2 Motor per Taster gestoppt");
        return;
    }

    // ===== ANDERER MOTOR LÄUFT? =====
    // Verhindert dass beide L298N-Kanäle gleichzeitig ziehen.
    if (isAnyMotorRunning())
    {
        addLog("Klappe2 Taster ignoriert – anderer Motor läuft");
        return;
    }

    // ===== TOGGLE =====
    if (door2Open)
    {
        door2Phase   = PHASE_CLOSING;
        motor2Reason = "manuell/Taster";
        startMotor2Close(door2ClosePosition);
        actionLock2  = true;
        addLog("Klappe2 Schließvorgang (Taster)");
    }
    else
    {
        door2Phase   = PHASE_OPENING;
        motor2Reason = "manuell/Taster";
        startMotor2Open(door2OpenPosition);
        actionLock2  = true;
        addLog("Klappe2 Öffnung (Taster)");
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
                    addLog("Rotlicht AUS (HW-Taster)");
                } else if (manualLightActive) {
                    manualLightActive = false;            // Locklicht aus
                    lightOff();
                    lightActive = false;
                    addLog("Locklicht manuell AUS (HW-Taster)");
                } else {
                    manualLightActive = true;             // Locklicht ein
                    lightOn();
                    lightActive = true;
                    addLog("Locklicht manuell AN (HW-Taster)");
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