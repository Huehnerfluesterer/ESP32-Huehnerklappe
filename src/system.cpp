#include "system.h"
#include "lux.h"
#include "light.h"
#include "motor.h"
#include "logger.h"
#include "pins.h"
#include <WiFi.h>
#include <Arduino.h>

// Forward-Deklarationen aus mqtt.cpp
void mqttPublishAvailability(const char *state);
bool mqttClientConnected();

// ==================================================
// GLOBALE VARIABLEN
// ==================================================
RTC_DS3231        rtc;
bool              rtcOk = false;

volatile bool otaInProgress = false;
volatile bool ioSafeState   = false;

bool errorWifi      = false;
bool errorMQTT      = false;
bool errorSensor    = false;

// Software-Watchdog
static unsigned long lastLoopFeed  = 0;
static bool          wdogArmed     = false;
#define WDOG_TIMEOUT_MS  15000UL   // 90 Sekunden – dann Neustart

// ==================================================
bool systemError()
{
    return errorWifi || errorMQTT || errorSensor;
}

void tpl5110Init()
{
    pinMode(TPL5110_DONE_PIN, OUTPUT);
    digitalWrite(TPL5110_DONE_PIN, LOW);
    Serial.println("✅ TPL5110 Hardware-Watchdog aktiv (GPIO " + String(TPL5110_DONE_PIN) + ")");
}

static unsigned long lastTplFeed = 0;

void wdogFeed()
{
    lastLoopFeed = millis();
    wdogArmed    = true;
    if (millis() - lastTplFeed >= 20000UL) {
        lastTplFeed = millis();
        digitalWrite(TPL5110_DONE_PIN, HIGH);
        delayMicroseconds(100);
        digitalWrite(TPL5110_DONE_PIN, LOW);
    }
}

void updateSystemHealth()
{
    errorWifi   = (WiFi.status() != WL_CONNECTED);
    errorSensor = (!hasVEML || vemlHardError);

    // Software-Watchdog prüfen
    if (wdogArmed && (millis() - lastLoopFeed > WDOG_TIMEOUT_MS)) {
        // Loop hat 90s nicht reagiert – Neustart
        addLog("⚠️ Watchdog: Loop-Timeout – Neustart");
        delay(500);
        ESP.restart();
    }
}

// ==================================================
// OTA SAFE STATE
// ==================================================
void enterIoSafeState()
{
    otaInProgress = true;
    ioSafeState   = true;

    motorStop();
    digitalWrite(MOTOR_IN1, LOW);
    digitalWrite(MOTOR_IN2, LOW);
    ledcWrite(3, 0);

    lightOff();
    stallLightOff();
    digitalWrite(RELAIS_PIN,          RELAY_OFF);
    digitalWrite(STALLLIGHT_RELAY_PIN, RELAY_OFF);

    lightActive      = false;
    manualLightActive = false;

    if (mqttClientConnected()) mqttPublishAvailability("offline");
    addLog("🛡️ I/O in sicheren Zustand versetzt (OTA)");
}

void leaveIoSafeState()
{
    ioSafeState = false;
    if (mqttClientConnected()) mqttPublishAvailability("online");
    addLog("✅ I/O Safe-State beendet");
}

// ==================================================
// SIMULATIONS-ZEITOFFSET
// ==================================================
static long simOffsetSeconds = 0;  // Offset in Sekunden (positiv = vorwärts)
static bool simActive = false;

void simSetOffset(int hours, int minutes)
{
    simOffsetSeconds = (long)hours * 3600L + (long)minutes * 60L;
    simActive = (simOffsetSeconds != 0);
    if (simActive)
        Serial.printf("🕐 Simulations-Offset: %+dh %+dmin (%+lds)\n", hours, minutes, simOffsetSeconds);
    else
        Serial.println("🕐 Simulations-Offset zurückgesetzt");
}

bool simIsActive() { return simActive; }
long simGetOffsetSeconds() { return simOffsetSeconds; }

DateTime nowRTC()
{
    DateTime base;
    if (rtcOk) {
        base = rtc.now();
    } else {
        struct tm ti;
        if (getLocalTime(&ti, 0))
            base = DateTime(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                            ti.tm_hour, ti.tm_min, ti.tm_sec);
        else
            base = DateTime(2000, 1, 1, 8, 0, 0);
    }
    if (!simActive) return base;
    return DateTime(base.unixtime() + simOffsetSeconds);
}
