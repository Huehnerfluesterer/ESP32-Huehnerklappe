#include "lux.h"
#include "logger.h"
#include "telegram.h"
#include "pins.h"
#include "system.h"          // NEU: für RESTART_VEML & lastRestartSource
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
static uint32_t      vemlReinitFailCount = 0;  // File-scope: Backoff-Index für reinitVEML7700()
static unsigned long lastVemlReinit  = 0;

// NEU: Notfall-Restart-Tracking
static unsigned long vemlHardErrorSince = 0;

// NEU: Stuck-Value-Detektion (Sensor antwortet, liefert aber konstant denselben Wert)
static float         stuckLastValue = NAN;
static unsigned long stuckSince     = 0;

// NEU: Detail-Diagnostik – welche Fehlerart wie oft?
// Wird in checkLuxHealth() alle 30 min ins Log geschrieben falls Fehler aufgetreten.
// So sehen wir ob das Problem I²C-Kommunikation, Sensor-Logik oder Wertebereich ist.
static uint32_t      luxStatAck       = 0;  // I²C ACK fehlt (Bus tot / Sensor nicht erreichbar)
static uint32_t      luxStatTimeout   = 0;  // readLux brauchte > 50ms (Bus blockiert)
static uint32_t      luxStatNaN       = 0;  // Sensor antwortet, Wert ist NaN/Inf
static uint32_t      luxStatOOR       = 0;  // Wert out-of-range (<0 oder >120000 lx)
static uint32_t      luxStatOK        = 0;  // gültige Messung
static unsigned long luxStatLastReport = 0;
static unsigned long luxStatLastAckLog = 0;
static unsigned long luxStatLastNaNLog = 0;
static unsigned long luxStatLastOORLog = 0;

static float luxHistory[20]      = {0};
static uint8_t luxHistoryIdx     = 0;

static float luxMedianBuf[5]     = {0};
static uint8_t luxMedianIdx      = 0;

const unsigned long VEML_REINIT_COOLDOWN      = 15000UL;
const unsigned long VEML_HARD_TIMEOUT         = 60000UL;
const unsigned long LUX_FALLBACK_AFTER_MS     = 5UL * 60UL * 1000UL;
const unsigned long VEML_EMERGENCY_RESTART_MS = 2UL * 60UL * 60UL * 1000UL; // 2h durchgehender Hard-Error → Reboot
const unsigned long VEML_STUCK_TIMEOUT_MS     = 30UL * 60UL * 1000UL;       // 30min identischer Wert → Stuck

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
        // NEU: Fehlertyp "ACK fehlt" zählen + throttled loggen (max 1x pro 5 min)
        luxStatAck++;
        if (millis() - luxStatLastAckLog > 300000UL) {
            addLog("⚠️ Lux-Diag: I²C ACK fehlt (err=" + String(err) + ")");
            luxStatLastAckLog = millis();
        }
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
        // Logspam-Schutz: In den ersten 10s nach Boot ist ein langsamer
        // erster Read normal (Sensor warm-up) – nicht ins Log schreiben.
        if (millis() > 10000UL) {
            addLog("⚠️ Lux-Sensor blockiert " + String(elapsed) + "ms – Reinit");
        }
        luxStatTimeout++;        // NEU: Counter
        luxBlockedUntil = millis() + 5000;
        hasVEML = false;  // → triggert reinitVEML7700() in checkLuxHealth
        return NAN;
    }

    // NEU: NaN/Inf und Out-Of-Range getrennt erfassen – verrät uns ob der Sensor
    // gar nichts liefert (NaN) oder unsinnige Werte produziert (OOR).
    if (!isfinite(val)) {
        luxStatNaN++;
        if (millis() - luxStatLastNaNLog > 300000UL) {
            addLog("⚠️ Lux-Diag: Sensor liefert NaN/Inf");
            luxStatLastNaNLog = millis();
        }
        return NAN;
    }
    if (val < 0 || val > 120000) {
        luxStatOOR++;
        if (millis() - luxStatLastOORLog > 300000UL) {
            addLog("⚠️ Lux-Diag: Wert out-of-range (" + String(val, 1) + " lx)");
            luxStatLastOORLog = millis();
        }
        return NAN;
    }

    // Gültige Messung
    luxStatOK++;
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
    // -----------------------------------------------------------
    // NEU: DIAGNOSE-STATISTIK alle 30 min loggen
    // Schreibt nur wenn in den letzten 30 min mind. 1 Fehler war.
    // So sehen wir auf einen Blick welche Fehlerart dominiert:
    //   ACK  = I²C-Kommunikation defekt (Verkabelung/EMV)
    //   TO   = Bus blockiert (Sensor hängt während Read)
    //   NaN  = Sensor antwortet, Wert ist ungültig (Sensor-IC defekt)
    //   OOR  = Sensor antwortet, Wert unsinnig (Sensor-IC marginal)
    //   OK   = Anzahl gültiger Messungen im Fenster (für Verhältnis)
    // -----------------------------------------------------------
    if (luxStatLastReport == 0) luxStatLastReport = nowMs;
    if (nowMs - luxStatLastReport >= 30UL * 60UL * 1000UL) {
        uint32_t totalFails = luxStatAck + luxStatTimeout + luxStatNaN + luxStatOOR;
        if (totalFails > 0) {
            String msg = "📊 Lux-Diag 30min: OK=" + String(luxStatOK)
                       + " ACK=" + String(luxStatAck)
                       + " TO="  + String(luxStatTimeout)
                       + " NaN=" + String(luxStatNaN)
                       + " OOR=" + String(luxStatOOR);
            addLog(msg);
        }
        luxStatOK = luxStatAck = luxStatTimeout = luxStatNaN = luxStatOOR = 0;
        luxStatLastReport = nowMs;
    }

    // -----------------------------------------------------------
    // STUCK-VALUE-DETEKTION
    // Sensor antwortet auf I2C, liefert aber konstant denselben Wert.
    // Beispiel aus Praxis: 3267.9 lx für Stunden eingefroren.
    // → nach VEML_STUCK_TIMEOUT_MS wie Hard-Error behandeln.
    //
    // FIX: Werte <= STUCK_MIN_LX (echte Dunkelheit) sind legitim konstant
    // und dürfen NICHT als Stuck gewertet werden – sonst feuert die
    // Detektion jede Nacht alle 30 min.
    // -----------------------------------------------------------
    static const float STUCK_MIN_LX = 1.0f;  // unterhalb gilt: echte Nacht, nicht stuck

    if (luxValid && isfinite(rawLux) && rawLux > STUCK_MIN_LX) {
        if (isfinite(stuckLastValue) && fabsf(rawLux - stuckLastValue) < 0.5f) {
            if (stuckSince > 0 && nowMs - stuckSince > VEML_STUCK_TIMEOUT_MS) {
                Serial.printf("⚠️ Lux stuck bei %.1f lx seit 30 min → als Fehler behandeln\n", rawLux);
                addLog("⚠️ VEML7700 Stuck-Value (" + String(rawLux, 1) + " lx seit 30 min)");
                luxValid = false;     // ab hier wie normaler Hard-Error behandeln
                hasVEML  = false;     // → triggert Reinit unten
                stuckSince = nowMs;   // damit es nicht jede Sekunde erneut feuert
            }
        } else {
            // Wert hat sich geändert → Tracking neu starten
            stuckLastValue = rawLux;
            stuckSince     = nowMs;
        }
    } else {
        // Dunkelheit (<= STUCK_MIN_LX) oder ungültig → Tracking pausieren,
        // damit beim Übergang in den hellen Bereich sauber neu gemessen wird.
        stuckLastValue = NAN;
        stuckSince     = 0;
    }

    // -----------------------------------------------------------
    // STANDARD-FEHLERZÄHLER
    // -----------------------------------------------------------
    if (!luxValid)
    {
        if (luxInvalidSince == 0) luxInvalidSince = nowMs;
        if (luxFailCount < 255)   luxFailCount++;
    }
    else
    {
        // NEU: Recovery-Erkennung – war vorher ein Fehler aktiv?
        bool wasError = vemlHardError || vemlSoftError;

        luxFailCount    = 0;
        luxInvalidSince = 0;
        vemlSoftError   = false;
        vemlHardError   = false;
        vemlHardErrorSince = 0;     // NEU: Notfall-Timer zurücksetzen
        vemlLastLux     = nowMs;
        vemlReinitFailCount = 0;    // Echter Erfolg → Backoff zurücksetzen

        // NEU: Telegram-Meldung "wieder OK", wenn vorher ein Fehler aktiv war
        if (wasError) {
            addLog("✅ VEML7700 wieder funktionsfähig");
            telegramSensorRecovered();
        }
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
        if (vemlHardErrorSince == 0) vemlHardErrorSince = nowMs;   // NEU: Notfall-Timer starten
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

    // -----------------------------------------------------------
    // NOTFALL-RESTART
    // Wenn der Sensor seit >2h durchgehend tot ist und sich keine
    // Selbstheilung einstellt, ESP komplett neu starten – statt bis
    // 03:00 Uhr zu warten (siehe logic.cpp). Reset des Timers passiert
    // automatisch oben im else-Zweig sobald ein gültiger Wert kommt.
    // -----------------------------------------------------------
    if (vemlHardError && vemlHardErrorSince > 0 &&
        nowMs - vemlHardErrorSince > VEML_EMERGENCY_RESTART_MS)
    {
        addLog("🔄 Notfall-Neustart – VEML7700 seit 2h tot");
        telegramSend("🔄 Notfall-Neustart\nVEML7700 reagiert seit 2 Stunden nicht – Reboot in 5s.");
        loggerUpdate();             // Logs sofort sichern vor Restart
        lastRestartSource = RESTART_VEML;
        delay(5000);
        ESP.restart();
    }

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
    Wire.setClock(50000);    // 50 kHz – robust gegen Leitungs-Kapazität (1,5m Cat5)
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
    static const unsigned long backoff[] = {15000UL, 30000UL, 60000UL, 300000UL, 900000UL, 3600000UL};
    static const int backoffSize = sizeof(backoff) / sizeof(backoff[0]);
    int idx = (vemlReinitFailCount < (uint32_t)backoffSize) ? vemlReinitFailCount : backoffSize - 1;
    unsigned long currentCooldown = backoff[idx];

    if (millis() - lastVemlReinit < currentCooldown) return;
    lastVemlReinit = millis();

    // I²C-Mutex nehmen: i2cBusRecover() reißt den Bus per Wire.end() +
    // Bit-Banging nieder. Läuft das gleichzeitig mit getLux() auf Core 0,
    // verklemmt die I2C-Peripherie → 120s-Hang → Task-WDT-Reboot.
    // Bekommen wir den Bus nicht (z.B. getLux hält ihn gerade), überspringen
    // wir diesen Versuch – der Backoff sorgt für den nächsten.
    if (!i2cTake(1000)) {
        Serial.println("⚠️ VEML Reinit: I2C-Bus belegt – überspringe");
        return;
    }

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
        luxInvalidSince = 0;        // ← KRITISCH: Hard-Error-Timer zurücksetzen
        vemlSoftError = false;
        vemlHardError = false;

        // NEU: Stuck-Tracking nach Reinit ebenfalls reset, sonst würde der
        // alte (eingefrorene) Wert sofort wieder als "stuck" gemeldet werden.
        stuckLastValue = NAN;
        stuckSince     = 0;

        // WICHTIG: vemlReinitFailCount NICHT hier auf 0 setzen!
        // veml.begin() Erfolg bedeutet nur dass der Chip auf I2C antwortet,
        // nicht dass das eigentliche Lesen klappt. Bei dauerhaft kaputtem
        // Sensor wäre das eine Endlosschleife alle 15s.
        // → Counter wird in checkLuxHealth zurückgesetzt, sobald ein
        //   wirklich gültiger Lese-Wert kommt.
        vemlReinitFailCount++;

        Serial.println("✅ VEML reinitialisiert");
        // Log-Throttle: maximal alle 5 Minuten ins Web-Log schreiben
        static unsigned long lastSuccessLog = 0;
        static bool wasLoggedOnce = false;
        if (wasLoggedOnce && (millis() - lastSuccessLog > 300000UL)) {
            addLog("✅ VEML7700 reinitialisiert");
            lastSuccessLog = millis();
        }
        wasLoggedOnce = true;
    }
    else
    {
        hasVEML = false;
        vemlReinitFailCount++;
        Serial.printf("❌ VEML Reinit fehlgeschlagen (Versuch %u, nächster in %lus)\n",
                      vemlReinitFailCount, backoff[idx < backoffSize - 1 ? idx + 1 : idx] / 1000);
        // Nur bei bestimmten Fail-Counts loggen, nicht jedes Mal
        if (vemlReinitFailCount == 1 || vemlReinitFailCount == 5 || vemlReinitFailCount == 20)
            addLog("⚠️ VEML7700 nicht erreichbar (Versuch " + String(vemlReinitFailCount) + ")");
    }

    // I²C-Bus freigeben – beide Zweige (Erfolg/Fehler) laufen hier zusammen.
    i2cGive();
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
            // I²C-Mutex nehmen: stellt sicher dass kein gleichzeitiger
            // Bus-Teardown im Reinit (Loop/Core 1) in dieses getLux() grätscht.
            // Dieser Task ist NICHT vom Task-WDT überwacht – ein langes Warten
            // oder Blockieren hier kann den ESP nicht neu starten.
            float val = NAN;
            if (i2cTake(3000)) {
                val = getLux();   // kann blockieren – egal, nur dieser Task hängt
                i2cGive();
            }
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