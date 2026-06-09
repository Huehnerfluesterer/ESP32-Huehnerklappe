#include "motor.h"
#include "light.h"    // lightState, startLightForMinutesReset, dimmingActive, ...
#include "door.h"     // doorOpen, doorPhase, doorOpenedAt
#include "lux.h"      // lux
#include "storage.h"  // lampPostOpen, lampPostClose, openMode, closeMode
#include "logger.h"
#include "telegram.h"
#include "pins.h"
#include <Arduino.h>

// ==================================================
// GLOBALE VARIABLEN (dieses Moduls)
// ==================================================
MotorState    motorState   = MOTOR_STOPPED;
unsigned long motorUntil   = 0;

// Tages-Statistik
int           statOpenCount      = 0;
int           statCloseCount     = 0;
unsigned long statOpenDurationMs = 0;
static unsigned long statOpenStart = 0;
String        motorReason  = "";

bool         actionLock     = false;
unsigned long limitOpenSince  = 0;
unsigned long limitCloseSince = 0;

// --- ACS712 Blockadeerkennung ---
float         currentBaseline    = 0.0f;  // Leerlauf-Strom beim Motorstart (A)
bool          currentCalibrated  = false; // Baseline eingemessen?
unsigned long motorStartedAt     = 0;     // Zeitpunkt Motorstart für Settle-Zeit
bool          blockadeEnabled    = true;  // wird aus EEPROM geladen
float         blockadeThresholdA = BLOCKADE_THRESHOLD_A; // wird aus EEPROM geladen
float         peakCurrentA       = 0.0f;  // höchster gemessener Strom seit letztem Reset

// Spannungsteiler-Korrektur: 10kΩ/20kΩ → Faktor 20/(10+20) = 0.6667
// ADC-Bereich 0–4095 entspricht 0–3.3V
// Rückrechnung auf Sensor-Ausgangsspannung: Vmeas / 0.6667
// Strom = (Vsensor - ACS712_ZERO_V) / (ACS712_MV_PER_A / 1000.0)
static float measureCurrentAmps()
{
    // ESP32 ADC ist rauschbehaftet → 100 Samples mitteln
    const int SAMPLES = 100;
    long sum = 0;
    for (int i = 0; i < SAMPLES; i++) sum += analogRead(ACS712_PIN);
    float adcAvg  = (float)sum / SAMPLES;
    float vMeas   = adcAvg * (3.3f / 4095.0f);
    // Spannungsteiler-Korrektur nur wenn verbaut (ACS712_HAS_DIVIDER = 1)
#if ACS712_HAS_DIVIDER
    float vSensor = vMeas / (20.0f / 30.0f);  // 10k/20k Teiler zurückrechnen
#else
    float vSensor = vMeas;                     // kein Teiler: direkt messen
#endif
    float amps    = fabsf((vSensor - ACS712_ZERO_V) / (ACS712_MV_PER_A / 1000.0f));
    if (amps > 8.0f) return 0.0f;  // Pin floatet / kein Sensor
    return amps;
}

static void calibrateBaseline()
{
    // 5 schnelle Messungen ohne delay()
    float sum = 0.0f;
    for (int i = 0; i < 5; i++) sum += measureCurrentAmps();
    currentBaseline   = sum / 5.0f;
    currentCalibrated = true;
    Serial.printf("⚡ ACS712 Baseline: %.2f A  (Blockade ab %.2f A)\n",
                  currentBaseline, currentBaseline + blockadeThresholdA);
}

const unsigned long LIMIT_DEBOUNCE_MS = 40;

// Vorwärts-Deklarationen aus anderen Modulen
void saveDoorState();
void addLogWithLux(const String &text, float lx);

// Interne Variablen aus logic.h (Prognose-Zustand zurücksetzen nach Öffnen/Schließen)
extern bool         preLightForecastActive;
extern bool         preLightForecastCondition;
extern unsigned long preLightStartedAt;
extern bool         preLightOpenDone;
extern bool         preLightCloseDone;
extern unsigned long lightBelowSince;
extern unsigned long closeInterruptionSince;
extern unsigned long plannedCloseAt;
extern bool         closeForecastCondition;
extern unsigned long closeForecastStableSince;
extern unsigned long lastDoorCloseTime;
extern unsigned long scheduledCloseAt;
extern bool          nightLock;

// ==================================================
void motorInit()
{
    pinMode(MOTOR_IN1, OUTPUT); digitalWrite(MOTOR_IN1, LOW);
    pinMode(MOTOR_IN2, OUTPUT); digitalWrite(MOTOR_IN2, LOW);
    pinMode(MOTOR_ENA, OUTPUT); digitalWrite(MOTOR_ENA, LOW);
    delay(10);
    ledcSetup(3, 2000, 8);
    ledcAttachPin(MOTOR_ENA, 3);
    ledcWrite(3, 0);
    Serial.println("✅ LEDC Motor-PWM initialisiert (GPIO " + String(MOTOR_ENA) + ", Kanal 3)");
}

// ==================================================
// HARDWARE-PRIMITIVES
// ==================================================
void motorStop()
{
    digitalWrite(MOTOR_IN1, LOW);
    digitalWrite(MOTOR_IN2, LOW);
    ledcWrite(3, 0);
}

void motorOpen()
{
    digitalWrite(MOTOR_IN1, HIGH);
    digitalWrite(MOTOR_IN2, LOW);
    ledcWrite(3, 180);
}

void motorClose()
{
    digitalWrite(MOTOR_IN1, LOW);
    digitalWrite(MOTOR_IN2, HIGH);
    ledcWrite(3, 180);
}

void startMotorOpen(unsigned long durationMs)
{
    // Mutual Exclusion: ACS712 wird geteilt
    if (motor2State != MOTOR_STOPPED) {
        addLog("⚠️ Klappe1: Öffnung blockiert – Klappe 2 Motor läuft");
        return;
    }
    motorOpen();
    motorState        = MOTOR_OPENING;
    motorUntil        = millis() + durationMs;
    // Blockadeerkennung: Kalibrierung nach Anlaufzeit
    currentCalibrated = false;
    motorStartedAt    = millis();
}

void startMotorClose(unsigned long durationMs)
{
    // Mutual Exclusion: ACS712 wird geteilt
    if (motor2State != MOTOR_STOPPED) {
        addLog("⚠️ Klappe1: Schließen blockiert – Klappe 2 Motor läuft");
        return;
    }
    // doorOpen wird ERST nach Abschluss des Schließvorgangs auf false gesetzt.
    // Bei einem Reset während des Schließens bleibt der Zustand somit "offen"
    // und die Automatik kann nach dem Boot korrekt erneut schließen.
    doorPhase = PHASE_CLOSING;
    saveDoorState();
    motorClose();
    motorState        = MOTOR_CLOSING;
    motorUntil        = millis() + durationMs;
    // Blockadeerkennung: Kalibrierung nach Anlaufzeit
    currentCalibrated = false;
    motorStartedAt    = millis();
}

// ==================================================
bool isManualAction()
{
    return motorReason.indexOf("manuell") >= 0 ||
           motorReason.indexOf("Taster")  >= 0 ||
           motorReason.indexOf("Web")     >= 0;
}

void reverseAfterBlockade()
{
    Serial.println("↩️ Rückwärtsfahren nach Blockade");
    // doorOpen wurde in startMotorClose() bereits auf false gesetzt –
    // da die Tür aber NICHT geschlossen ist, korrigieren wir das hier
    doorOpen = true;
    saveDoorState();
    lightState = LIGHT_POST_OPEN;
    startLightForMinutes(lampPostOpen);
    lightOn();
    lightActive = true;
    motorOpen();
    motorState = MOTOR_OPENING;
    motorUntil = millis() + 800;
}

// ==================================================
// UPDATE (zyklisch in loop())
// ==================================================
void updateMotor()
{
    if (motorState == MOTOR_STOPPED) return;

    // ===== STROMMESSUNG (immer, unabhängig von Blockadeerkennung) =====
    // Peak-Tracking läuft immer mit sobald Motor läuft
    static unsigned long lastCurrentCheck = 0;
    if (millis() - motorStartedAt > 200UL && millis() - lastCurrentCheck > 200UL)
    {
        lastCurrentCheck = millis();
        float ampsNow = measureCurrentAmps();
        if (ampsNow > peakCurrentA) peakCurrentA = ampsNow;
    }

    // ===== BLOCKADEERKENNUNG (ACS712) =====
    // Nach 500ms Anlaufzeit: Baseline einmessen; danach alle 200ms prüfen
    static unsigned long lastBlockadeCheck = 0;
    if (millis() - motorStartedAt > 500UL)
    {
        if (!currentCalibrated)
        {
            calibrateBaseline();  // immer einmessen, auch wenn Erkennung deaktiviert
        }
        else if (blockadeEnabled && millis() - lastBlockadeCheck > 200UL)
        {
            lastBlockadeCheck = millis();
            float amps = measureCurrentAmps();
            if (!isnan(blockadeThresholdA) && amps > currentBaseline + blockadeThresholdA)
            {
                Serial.printf("🚨 Blockade! %.2f A (Baseline %.2f A)\n",
                              amps, currentBaseline);
                addLog(String("Blockade erkannt (") + String(amps, 1) + "A)");
                motorStop();
                motorState        = MOTOR_STOPPED;
                currentCalibrated = false;
                reverseAfterBlockade();
                return;
            }
        }
    }

    // ===== ENDSCHALTER ÖFFNEN =====
    if (useLimitSwitches && motorState == MOTOR_OPENING)
    {
        if (digitalRead(LIMIT_OPEN_PIN) == LOW)
        {
            if (limitOpenSince == 0) limitOpenSince = millis();
            if (millis() - limitOpenSince > LIMIT_DEBOUNCE_MS)
            {
                motorStop();
                doorOpen     = true;
                doorPhase    = PHASE_OPEN;
                actionLock   = false;
                doorOpenedAt = millis();
                statOpenCount++; statOpenStart = millis();
                saveDoorState();
                addLog("Endschalter OBEN erreicht");
                motorState    = MOTOR_STOPPED;
                limitOpenSince = 0;

                // Post-Licht
                if (!isManualAction())
                {
                    lightState = LIGHT_POST_OPEN;
                    startLightForMinutesReset(lampPostOpen);
                    lightOn();
                    lightActive = true;
                    addLogWithLux("Locklicht nach Öffnung gestartet", lux);
                }
                preLightForecastActive    = false;
                preLightForecastCondition = false;
                preLightStartedAt         = 0;
                lightBelowSince           = 0;
                closeInterruptionSince    = 0;
                plannedCloseAt            = 0;
                motorReason               = "";
                return;
            }
        }
        else { limitOpenSince = 0; }
    }

    // ===== ENDSCHALTER SCHLIESSEN =====
    if (useLimitSwitches && motorState == MOTOR_CLOSING)
    {
        if (digitalRead(LIMIT_CLOSE_PIN) == LOW)
        {
            if (limitCloseSince == 0) limitCloseSince = millis();
            if (millis() - limitCloseSince > LIMIT_DEBOUNCE_MS)
            {
                motorStop();
                doorOpen   = false;
                doorPhase  = PHASE_IDLE;
                actionLock = false;
                if (statOpenStart > 0) { statOpenDurationMs += millis() - statOpenStart; statOpenStart = 0; }
                statCloseCount++;
                saveDoorState();
                addLog("Endschalter UNTEN erreicht");
                motorState     = MOTOR_STOPPED;
                limitCloseSince = 0;

                // Nacht-Sperre + Post-Licht
                lastDoorCloseTime      = millis();
                preLightOpenDone       = false;
                preLightCloseDone      = true;
                scheduledCloseAt       = 0;
                closeForecastCondition = false;
                closeForecastStableSince = 0;
                addLogWithLux("Klappe geschlossen (Endschalter)", lux);

                if (openMode == "light" && closeMode == "light")
                {
                    nightLock = true;
                    addLog("Nacht-Sperre aktiv");
                }
                // Settle-Light (POST_CLOSE) starten wenn:
                //   • automatisches Schließen (klassischer Fall), ODER
                //   • manuelles Schließen während die Vor-Schließen-Phase lief.
                // Damit bekommen die Hühner auch nach manuellem Schließen am Abend
                // ihre 15-min-Dimmphase zum Eingewöhnen. Bei manuellem Schließen
                // ohne aktive Vor-Phase (z.B. tagsüber) bleibt's wie bisher: kein POST_CLOSE.
                if (!isManualAction() || lightState == LIGHT_PRE_CLOSE)
                {
                    lightState = LIGHT_POST_CLOSE;
                    startLightForMinutesReset(lampPostClose);
                    lightOn();
                    lightActive = true;
                    addLogWithLux("Locklicht nach Schließen gestartet (" + String(lampPostClose) + " min)", lux);
                    if (lampPostClose >= 5)
                    {
                        dimEndTime    = lightStateUntil;
                        dimStartTime  = dimEndTime - 5UL * 60000UL;
                        dimmingActive = true;
                    }
                    else dimmingActive = false;
                }
                motorReason = "";
                return;
            }
        }
        else { limitCloseSince = 0; }
    }

    // ===== TIMEOUT =====
    if (millis() - motorStartedAt >= (motorUntil - motorStartedAt))
    {
        motorStop();

        if (motorState == MOTOR_OPENING)
        {
            doorOpen     = true;
            doorPhase    = PHASE_OPEN;
            actionLock   = false;
            doorOpenedAt = millis();
            statOpenCount++; statOpenStart = millis();
            saveDoorState();

            if (motorReason.indexOf("Licht") >= 0)
                addLogWithLux("Klappe geöffnet (" + motorReason + ")", lux);
            else
                addLog("Klappe geöffnet (" + motorReason + ")");

            telegramDoorOpened(motorReason);

            preLightForecastActive    = false;
            preLightForecastCondition = false;
            preLightStartedAt         = 0;

            if (!isManualAction())
            {
                lightState = LIGHT_POST_OPEN;
                startLightForMinutesReset(lampPostOpen);
                lightOn();
                lightActive = true;
                addLogWithLux("Locklicht nach Öffnung gestartet", lux);
            }
            lightBelowSince        = 0;
            closeInterruptionSince = 0;
            plannedCloseAt         = 0;
            motorReason            = "";
        }
        else if (motorState == MOTOR_CLOSING)
        {
            doorOpen   = false;
            doorPhase  = PHASE_IDLE;
            actionLock = false;
            if (statOpenStart > 0) { statOpenDurationMs += millis() - statOpenStart; statOpenStart = 0; }
            statCloseCount++;
            saveDoorState();

            lightBelowSince          = 0;
            preLightOpenDone         = false;
            preLightCloseDone        = true;
            scheduledCloseAt         = 0;
            closeForecastCondition   = false;
            closeForecastStableSince = 0;
            lastDoorCloseTime        = millis();

            addLogWithLux("Klappe geschlossen (" + motorReason + ")", lux);
            telegramDoorClosed(motorReason, lux);

            if (openMode == "light" && closeMode == "light")
            {
                nightLock = true;
                addLog("Nacht-Sperre aktiv (durch Abschluss Schließen)");
            }
            // Settle-Light auch nach manuellem Schließen wenn PRE_CLOSE lief
            // (siehe Erklärung im Endschalter-Block oben).
            if (!isManualAction() || lightState == LIGHT_PRE_CLOSE)
            {
                lightState = LIGHT_POST_CLOSE;
                startLightForMinutesReset(lampPostClose);
                lightOn();
                lightActive = true;
                addLogWithLux("Locklicht nach Schließen gestartet (" + String(lampPostClose) + " min)", lux);
                if (lampPostClose >= 5)
                {
                    dimEndTime    = lightStateUntil;
                    dimStartTime  = dimEndTime - 5UL * 60000UL;
                    dimmingActive = true;
                }
                else dimmingActive = false;
            }
            motorReason = "";
        }
        motorState = MOTOR_STOPPED;
    }
}

// ==================================================
// KLAPPE 2 – L298N KANAL B
// ==================================================

MotorState    motor2State   = MOTOR_STOPPED;
unsigned long motor2Until   = 0;
String        motor2Reason  = "";

bool         actionLock2     = false;
unsigned long limit2OpenSince  = 0;
unsigned long limit2CloseSince = 0;

long door2OpenPosition  = 6000;
long door2ClosePosition = 6000;
bool door2UseLimitSwitches = false;
unsigned long motor2StartedAt = 0;
float door2BlockadeThresholdA = BLOCKADE_THRESHOLD_A;

// Tages-Statistik Klappe 2
static int           stat2OpenCount      = 0;
static int           stat2CloseCount     = 0;
static unsigned long stat2OpenDurationMs = 0;
static unsigned long stat2OpenStart      = 0;

// ==================================================
bool isAnyMotorRunning()
{
    return (motorState != MOTOR_STOPPED) || (motor2State != MOTOR_STOPPED);
}

void motor2Init()
{
    pinMode(MOTOR2_IN1, OUTPUT); digitalWrite(MOTOR2_IN1, LOW);
    pinMode(MOTOR2_IN2, OUTPUT); digitalWrite(MOTOR2_IN2, LOW);
    pinMode(MOTOR2_ENB, OUTPUT); digitalWrite(MOTOR2_ENB, LOW);
    delay(10);
    ledcSetup(4, 2000, 8);
    ledcAttachPin(MOTOR2_ENB, 4);
    ledcWrite(4, 0);
    Serial.println("✅ LEDC Motor2-PWM initialisiert (GPIO " + String(MOTOR2_ENB) + ", Kanal 4)");
}

void motor2Stop()
{
    digitalWrite(MOTOR2_IN1, LOW);
    digitalWrite(MOTOR2_IN2, LOW);
    ledcWrite(4, 0);
}

void motor2Open()
{
    digitalWrite(MOTOR2_IN1, HIGH);
    digitalWrite(MOTOR2_IN2, LOW);
    ledcWrite(4, 180);
}

void motor2Close()
{
    digitalWrite(MOTOR2_IN1, LOW);
    digitalWrite(MOTOR2_IN2, HIGH);
    ledcWrite(4, 180);
}

void startMotor2Open(unsigned long durationMs)
{
    // Mutual Exclusion: ACS712 wird geteilt
    if (motorState != MOTOR_STOPPED) {
        addLog("⚠️ Klappe2: Öffnung blockiert – Klappe 1 Motor läuft");
        return;
    }
    motor2Open();
    motor2State       = MOTOR_OPENING;
    motor2Until       = millis() + durationMs;
    currentCalibrated = false;
    motor2StartedAt   = millis();
    motorStartedAt    = millis();  // ACS712 Timing gemeinsam
}

void startMotor2Close(unsigned long durationMs)
{
    // Mutual Exclusion: ACS712 wird geteilt
    if (motorState != MOTOR_STOPPED) {
        addLog("⚠️ Klappe2: Schließen blockiert – Klappe 1 Motor läuft");
        return;
    }
    door2Open = false;
    saveDoor2State();
    motor2Close();
    motor2State       = MOTOR_CLOSING;
    motor2Until       = millis() + durationMs;
    currentCalibrated = false;
    motor2StartedAt   = millis();
    motorStartedAt    = millis();  // ACS712 Timing gemeinsam
}

bool isManualAction2()
{
    return motor2Reason.indexOf("manuell") >= 0 ||
           motor2Reason.indexOf("Taster")  >= 0 ||
           motor2Reason.indexOf("Web")     >= 0;
}

void reverseAfterBlockade2()
{
    Serial.println("↩️ Rückwärtsfahren Klappe2 nach Blockade");
    door2Open = true;
    saveDoor2State();
    motor2Open();
    motor2State = MOTOR_OPENING;
    motor2Until = millis() + 800;
}

// ==================================================
// UPDATE MOTOR 2 (zyklisch in loop())
// ==================================================
void updateMotor2()
{
    if (motor2State == MOTOR_STOPPED) return;

    // ===== STROMMESSUNG (immer, unabhängig von Blockadeerkennung) =====
    static unsigned long lastCurrentCheck2 = 0;
    if (millis() - motor2StartedAt > 200UL && millis() - lastCurrentCheck2 > 200UL)
    {
        lastCurrentCheck2 = millis();
        float ampsNow = measureCurrentAmps();
        if (ampsNow > peakCurrentA) peakCurrentA = ampsNow;
    }

    // ===== BLOCKADEERKENNUNG (ACS712 – gemeinsam) =====
    static unsigned long lastBlockadeCheck2 = 0;
    if (millis() - motor2StartedAt > 500UL)
    {
        if (!currentCalibrated)
        {
            calibrateBaseline();
        }
        else if (blockadeEnabled && millis() - lastBlockadeCheck2 > 200UL)
        {
            lastBlockadeCheck2 = millis();
            float amps = measureCurrentAmps();
            if (!isnan(door2BlockadeThresholdA) && amps > currentBaseline + door2BlockadeThresholdA)
            {
                Serial.printf("🚨 Blockade Klappe2! %.2f A (Baseline %.2f A)\n",
                              amps, currentBaseline);
                addLog(String("Blockade Klappe2 (") + String(amps, 1) + "A)");
                motor2Stop();
                motor2State       = MOTOR_STOPPED;
                currentCalibrated = false;
                reverseAfterBlockade2();
                return;
            }
        }
    }

    // ===== ENDSCHALTER ÖFFNEN (Klappe 2) =====
    if (door2UseLimitSwitches && motor2State == MOTOR_OPENING)
    {
        if (digitalRead(LIMIT2_OPEN_PIN) == LOW)
        {
            if (limit2OpenSince == 0) limit2OpenSince = millis();
            if (millis() - limit2OpenSince > LIMIT_DEBOUNCE_MS)
            {
                motor2Stop();
                door2Open     = true;
                door2Phase    = PHASE_OPEN;
                actionLock2   = false;
                stat2OpenCount++; stat2OpenStart = millis();
                saveDoor2State();
                addLog("Endschalter Klappe2 OBEN erreicht");
                motor2State    = MOTOR_STOPPED;
                limit2OpenSince = 0;
                motor2Reason    = "";
                return;
            }
        }
        else { limit2OpenSince = 0; }
    }

    // ===== ENDSCHALTER SCHLIESSEN (Klappe 2) =====
    if (door2UseLimitSwitches && motor2State == MOTOR_CLOSING)
    {
        if (digitalRead(LIMIT2_CLOSE_PIN) == LOW)
        {
            if (limit2CloseSince == 0) limit2CloseSince = millis();
            if (millis() - limit2CloseSince > LIMIT_DEBOUNCE_MS)
            {
                motor2Stop();
                door2Open   = false;
                door2Phase  = PHASE_IDLE;
                actionLock2 = false;
                if (stat2OpenStart > 0) { stat2OpenDurationMs += millis() - stat2OpenStart; stat2OpenStart = 0; }
                stat2CloseCount++;
                saveDoor2State();
                addLog("Endschalter Klappe2 UNTEN erreicht");
                motor2State     = MOTOR_STOPPED;
                limit2CloseSince = 0;
                motor2Reason    = "";
                return;
            }
        }
        else { limit2CloseSince = 0; }
    }

    // ===== TIMEOUT =====
    if (millis() - motor2StartedAt >= (motor2Until - motor2StartedAt))
    {
        motor2Stop();

        if (motor2State == MOTOR_OPENING)
        {
            door2Open     = true;
            door2Phase    = PHASE_OPEN;
            actionLock2   = false;
            stat2OpenCount++; stat2OpenStart = millis();
            saveDoor2State();

            addLog("Klappe2 geöffnet (" + motor2Reason + ")");
            motor2Reason = "";
        }
        else if (motor2State == MOTOR_CLOSING)
        {
            door2Open   = false;
            door2Phase  = PHASE_IDLE;
            actionLock2 = false;
            if (stat2OpenStart > 0) { stat2OpenDurationMs += millis() - stat2OpenStart; stat2OpenStart = 0; }
            stat2CloseCount++;
            saveDoor2State();

            addLog("Klappe2 geschlossen (" + motor2Reason + ")");
            motor2Reason = "";
        }
        motor2State = MOTOR_STOPPED;
    }
}