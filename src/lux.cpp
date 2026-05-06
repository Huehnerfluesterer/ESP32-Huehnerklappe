#include "lux.h"
#include "logger.h"
#include "telegram.h"
#include "pins.h"
#include <Adafruit_VEML7700.h>
#include <Wire.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ==================================================
// LUX-TASK auf Core 0
// ==================================================
// Liest den VEML7700 alle 1s in einem eigenen FreeRTOS-Task auf Core 0.
// Wenn der Sensor hängt, blockiert nur dieser Task – der Haupt-Loop auf
// Core 1 läuft weiter und der WDT feuert nicht.
// ==================================================
static volatile float    luxTaskLatest    = NAN;
static volatile uint32_t luxTaskUpdatedMs = 0;
static TaskHandle_t      luxTaskHandle    = NULL;

// Forward-Deklarationen (Definitionen weiter unten)
static bool  simLuxActive  = false;
static float simLuxValue   = 0.0f;

// ==================================================
// GLOBALE VARIABLEN (dieses Moduls)
// ==================================================
float lux  = 0.0f;
bool  hasVEML      = false;
bool  vemlHardError  = false;
bool  vemlSoftError  = false;
float luxTrend       = 0.0f;
float luxRateFiltered = 0.0f;
bool  luxReady       = false;

// FIX: auf millis() initialisieren statt 0.
// Mit 0 wäre (nowMs - vemlLastLux) nach 5 Minuten sofort > LUX_FALLBACK_AFTER_MS,
// selbst wenn der Sensor einwandfrei funktioniert.
unsigned long vemlLastLux          = 0;  // wird in luxInit() auf millis() gesetzt
bool          lightAutomationAvailable = false;

float         lastLux  = 0.0f;
unsigned long lastLuxTime = 0;

bool          forecastTestMode  = false;
float         testLuxStart      = 200.0f;
float         testLuxEnd        = 5.0f;
float         testDurationMin   = 20.0f;
unsigned long testStartMillis   = 0;

// ==================================================
// INTERN
// ==================================================
static Adafruit_VEML7700 veml;

static uint8_t vemlGain = VEML7700_GAIN_1_8;
static uint8_t vemlIT   = VEML7700_IT_25MS;

static uint8_t       luxFailCount   = 0;
static unsigned long luxInvalidSince = 0;
static unsigned long lastVemlReinit  = 0;

static float luxHistory[20]      = {0};
static uint8_t luxHistoryIdx     = 0;

static float luxMedianBuf[5]     = {0};
static uint8_t luxMedianIdx      = 0;

const unsigned long VEML_REINIT_COOLDOWN = 15000UL;
const unsigned long VEML_HARD_TIMEOUT    = 60000UL;
const unsigned long LUX_FALLBACK_AFTER_MS = 5UL * 60UL * 1000UL;

// ==================================================
bool luxInit()
{
    // FIX: vemlLastLux auf jetzt setzen damit der 5-Minuten-Fallback-Timer
    // ab Boot läuft, nicht ab Zeitstempel 0.
    vemlLastLux = millis();

    hasVEML = veml.begin();
    if (hasVEML)
    {
        veml.setGain(VEML7700_GAIN_2);        // höhere Empfindlichkeit für schwaches Licht
        veml.setIntegrationTime(VEML7700_IT_100MS);
        vemlGain = VEML7700_GAIN_2;
        vemlIT   = VEML7700_IT_100MS;
        Serial.println("✅ VEML7700 initialisiert");
    }
    else
    {
        Serial.println("⚠️ VEML7700 nicht gefunden");
    }
    return hasVEML;
}

float getLux()
{
    // Manueller Sim-Override hat höchste Priorität
    if (simLuxIsActive()) return simLuxGetValue();
    if (!hasVEML) return NAN;

    // Cooldown nach Hänger – verhindert tight loop in den nächsten Versuch
    static unsigned long luxBlockedUntil = 0;
    if (millis() < luxBlockedUntil) return NAN;

    // Schneller Ping vor jedem Lese-Versuch
    Wire.setTimeOut(10);
    Wire.beginTransmission(0x10);
    uint8_t err = Wire.endTransmission();
    if (err != 0) {
        Wire.setTimeOut(20);
        // Sensor antwortet nicht mehr → als ausgefallen markieren,
        // damit checkLuxHealth den Reinit über i2cBusRecover() triggert
        hasVEML = false;
        return NAN;
    }

    // Hartes Zeit-Budget für readLux: max. 50ms.
    Wire.setTimeOut(15);
    unsigned long t0 = millis();
    float val = veml.readLux();
    unsigned long elapsed = millis() - t0;
    Wire.setTimeOut(20);

    if (elapsed > 50) {
        Serial.printf("⚠️ readLux brauchte %lums – Sensor-Reinit\n", elapsed);
        addLog("⚠️ Lux-Sensor blockiert " + String(elapsed) + "ms – Reinit");
        luxBlockedUntil = millis() + 5000;
        hasVEML = false;  // → triggert reinitVEML7700() in checkLuxHealth
        return NAN;
    }

    if (!isfinite(val) || val < 0 || val > 120000) return NAN;
    autoRangeVEML(val);
    return val;
}

float medianLux(float newValue)
{
    luxMedianBuf[luxMedianIdx] = newValue;
    luxMedianIdx = (luxMedianIdx + 1) % 5;

    float tmp[5];
    memcpy(tmp, luxMedianBuf, sizeof(tmp));
    // einfacher Bubble-Sort
    for (int i = 0; i < 4; i++)
        for (int j = i + 1; j < 5; j++)
            if (tmp[j] < tmp[i]) { float t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t; }
    return tmp[2];
}

void updateLuxTrend(float currentLux)
{
    luxHistory[luxHistoryIdx] = currentLux;
    luxHistoryIdx = (luxHistoryIdx + 1) % 20;
    float oldest = luxHistory[luxHistoryIdx];
    if (oldest == 0) return;
    luxTrend = currentLux - oldest;
}

// ==================================================
// FEHLERÜBERWACHUNG
// ==================================================
void checkLuxHealth(unsigned long nowMs, float rawLux, bool luxValid)
{
    if (!luxValid)
    {
        if (luxInvalidSince == 0) luxInvalidSince = nowMs;
        if (luxFailCount < 255)   luxFailCount++;
    }
    else
    {
        luxFailCount    = 0;
        luxInvalidSince = 0;
        vemlSoftError   = false;
        vemlHardError   = false;
        vemlLastLux     = nowMs;
    }

    // Soft-Error
    if (hasVEML && luxFailCount >= 3 && !vemlSoftError)
        vemlSoftError = true;

    // Hard-Error: 60 Sekunden keine gültigen Daten
    if (hasVEML && luxInvalidSince > 0 &&
        nowMs - luxInvalidSince > VEML_HARD_TIMEOUT && !vemlHardError)
    {
        vemlHardError = true;
        hasVEML       = false;
        Serial.println("❌ VEML Hard-Error");
        // Nur einmal loggen, nicht bei jedem Reinit-Zyklus erneut
        static unsigned long lastHardErrorLog = 0;
        if (nowMs - lastHardErrorLog > 600000UL) {  // max alle 10 min
            addLog("⚠️ VEML7700 Hard-Error (kein gültiger Wert seit 60s)");
            lastHardErrorLog = nowMs;
            telegramSensorError();
        }
    }

    // Periodischer Reinit-Versuch solange Sensor ausgefallen.
    // Läuft alle VEML_REINIT_COOLDOWN (15s) – ermöglicht Selbstheilung
    // nach I2C-Hänger, Spannungseinbruch oder kurzzeitiger Störung.
    if (!hasVEML && nowMs - lastVemlReinit > VEML_REINIT_COOLDOWN)
        reinitVEML7700();

    // Verfügbarkeit für Lichtautomatik
    lightAutomationAvailable = hasVEML && !vemlHardError &&
                               (nowMs - vemlLastLux) < LUX_FALLBACK_AFTER_MS;
}

// ==================================================
// I2C RECOVERY
// ==================================================
void i2cBusRecover()
{
    Serial.println("⚠️ I2C BUS RECOVERY START");
    pinMode(I2C_SDA, INPUT_PULLUP);
    pinMode(I2C_SCL, INPUT_PULLUP);
    delay(10);

    pinMode(I2C_SCL, OUTPUT);
    for (int i = 0; i < 9; i++)
    {
        digitalWrite(I2C_SCL, HIGH); delayMicroseconds(5);
        digitalWrite(I2C_SCL, LOW);  delayMicroseconds(5);
    }
    pinMode(I2C_SDA, OUTPUT);
    digitalWrite(I2C_SDA, LOW);  delayMicroseconds(5);
    digitalWrite(I2C_SCL, HIGH); delayMicroseconds(5);
    digitalWrite(I2C_SDA, HIGH);
    delay(10);

    Wire.end();
    delay(20);
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);   // ZURÜCK auf 100 kHz (war 30 kHz – verlangsamte alles!)
    Wire.setTimeOut(20);     // ZURÜCK auf 20ms
    Serial.println("✅ I2C BUS RECOVERY DONE");
}

// ==================================================
// REINIT
// ==================================================
void reinitVEML7700()
{
    // Exponentielles Backoff: 15s → 30s → 1min → 5min → 15min → 60min
    // Verhindert Logspam und unnötige I2C-Aktivität bei dauerhaft defektem Sensor
    static uint32_t reinitFailCount = 0;
    static const unsigned long backoff[] = {15000UL, 30000UL, 60000UL, 300000UL, 900000UL, 3600000UL};
    static const int backoffSize = sizeof(backoff) / sizeof(backoff[0]);
    int idx = (reinitFailCount < (uint32_t)backoffSize) ? reinitFailCount : backoffSize - 1;
    unsigned long currentCooldown = backoff[idx];

    if (millis() - lastVemlReinit < currentCooldown) return;
    lastVemlReinit = millis();

    Serial.println("🔄 VEML Reinit...");
    i2cBusRecover();

    if (veml.begin())
    {
        veml.setGain(VEML7700_GAIN_2);
        veml.setIntegrationTime(VEML7700_IT_100MS);
        vemlGain = VEML7700_GAIN_2;
        vemlIT   = VEML7700_IT_100MS;

        hasVEML     = true;
        luxFailCount = 0;
        vemlSoftError = false;
        vemlHardError = false;
        reinitFailCount = 0;  // Reset bei Erfolg

        Serial.println("✅ VEML reinitialisiert");
        // Nur loggen wenn der Sensor vorher schon mal erfolgreich war
        // (verhindert Spam beim Erstboot ohne Sensor)
        static bool wasLoggedOnce = false;
        if (wasLoggedOnce) addLog("✅ VEML7700 reinitialisiert");
        wasLoggedOnce = true;
    }
    else
    {
        hasVEML = false;
        reinitFailCount++;
        Serial.printf("❌ VEML Reinit fehlgeschlagen (Versuch %u, nächster in %lus)\n",
                      reinitFailCount, backoff[idx < backoffSize - 1 ? idx + 1 : idx] / 1000);
        // Nur bei bestimmten Fail-Counts loggen, nicht jedes Mal
        if (reinitFailCount == 1 || reinitFailCount == 5 || reinitFailCount == 20)
            addLog("⚠️ VEML7700 nicht erreichbar (Versuch " + String(reinitFailCount) + ")");
    }
}

// ==================================================
// AUTO-RANGE
// ==================================================
void autoRangeVEML(float luxValue)
{
    if (luxValue > 20000 && vemlGain != VEML7700_GAIN_1_8)
    {
        vemlGain = VEML7700_GAIN_1_8;
        veml.setGain(vemlGain);
    }
    else if (luxValue < 10 && vemlGain != VEML7700_GAIN_2)
    {
        vemlGain = VEML7700_GAIN_2;
        veml.setGain(vemlGain);
    }
    else if (luxValue >= 10 && luxValue <= 20000 && vemlGain != VEML7700_GAIN_1)
    {
        vemlGain = VEML7700_GAIN_1;
        veml.setGain(vemlGain);
    }
}

// ==================================================
// MANUELLER LUX-OVERRIDE (Simulation)
// ==================================================
void simLuxSet(float value)
{
    simLuxValue  = value;
    simLuxActive = true;
    Serial.printf("💡 Sim-Lux Override: %.1f lx\n", value);
}

void simLuxClear()
{
    simLuxActive = false;
    Serial.println("💡 Sim-Lux Override deaktiviert");
}

bool  simLuxIsActive()  { return simLuxActive; }
float simLuxGetValue()  { return simLuxValue; }

// ==================================================
// LUX-TASK (Core 0)
// ==================================================
// Läuft separat vom Loop. Ein blockierender readLux() hängt nur
// diesen Task auf, nicht den Hauptloop → kein WDT-Reset mehr durch I2C.
static void luxTaskFunction(void *param)
{
    Serial.println("✅ Lux-Task gestartet auf Core 0");
    for (;;) {
        if (hasVEML) {
            float val = getLux();   // kann blockieren – egal, nur dieser Task hängt
            if (isfinite(val)) {
                luxTaskLatest    = val;
                luxTaskUpdatedMs = millis();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));   // 1 Hz
    }
}

void luxTaskStart()
{
    if (luxTaskHandle) return;
    xTaskCreatePinnedToCore(
        luxTaskFunction,
        "luxTask",
        4096,           // Stack
        NULL,
        1,              // Priorität (niedrig – darf vom Loop verdrängt werden)
        &luxTaskHandle,
        0               // Core 0 (Loop läuft auf Core 1)
    );
}

float luxTaskGetValue()
{
    // Wenn der letzte gültige Wert älter als 30s ist → NAN
    // (Sensor hängt oder Task wurde gekillt)
    if (millis() - luxTaskUpdatedMs > 30000UL) return NAN;
    return luxTaskLatest;
}