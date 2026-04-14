#include "system.h"
#include "lux.h"
#include "light.h"
#include "motor.h"
#include "logger.h"
#include "pins.h"
#include "storage.h"   // mqttSettings.enabled
#include <WiFi.h>
#include <Arduino.h>
#include "esp_task_wdt.h"

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

// ==================================================
// HARDWARE-WATCHDOG (ESP32 Task-WDT)
// ==================================================
// Ersetzt den früheren TPL5110-Hardware-Watchdog. Der ESP32-S3 hat einen
// internen Watchdog im RTC-Bereich, der unabhängig von der CPU läuft.
// Wird wdogFeed() länger als WDOG_TIMEOUT_MS nicht aufgerufen, kommt ein
// sauberer Reset (Reset-Reason: ESP_RST_TASK_WDT).
#define WDOG_TIMEOUT_MS  30000UL   // 30 s ohne Feed → Reset

static unsigned long lastLoopFeed = 0;
static bool          wdogArmed    = false;

// ==================================================
bool systemError()
{
    return errorWifi || errorMQTT || errorSensor;
}

// Funktionsname bewusst beibehalten, damit main.cpp unverändert bleibt.
void tpl5110Init()
{
#if ESP_IDF_VERSION_MAJOR >= 5
    // Arduino-ESP32 v3.x / ESP-IDF v5.x – neue Struct-API
    esp_task_wdt_config_t cfg = {
        .timeout_ms     = WDOG_TIMEOUT_MS,
        .idle_core_mask = 0,
        .trigger_panic  = true,
    };
    if (esp_task_wdt_reconfigure(&cfg) == ESP_ERR_INVALID_STATE) {
        esp_task_wdt_init(&cfg);
    }
#else
    // Arduino-ESP32 v2.x / ESP-IDF v4.x – alte API (Sekunden, bool)
    esp_task_wdt_init(WDOG_TIMEOUT_MS / 1000, true);
#endif
    esp_task_wdt_add(NULL);   // aktuelle (Loop-)Task überwachen
    Serial.printf("✅ Task-Watchdog aktiv (Timeout %lu ms)\n", WDOG_TIMEOUT_MS);
}

void wdogFeed()
{
    lastLoopFeed = millis();
    wdogArmed    = true;
    esp_task_wdt_reset();
}

void updateSystemHealth()
{
    errorWifi   = (WiFi.status() != WL_CONNECTED);
    errorMQTT   = (mqttSettings.enabled && !mqttClientConnected());
    errorSensor = (!hasVEML || vemlHardError);

    // Watchdog läuft jetzt in Hardware (esp_task_wdt) – nichts zu tun
    (void)lastLoopFeed; (void)wdogArmed;
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