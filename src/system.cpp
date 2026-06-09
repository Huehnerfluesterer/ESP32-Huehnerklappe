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
// CRASH-DIAGNOSTIK (überlebt Restart in RTC-Memory)
// ==================================================
// WICHTIG: RTC_NOINIT_ATTR (statt RTC_DATA_ATTR), weil RTC_DATA_ATTR
// die Werte beim Boot wieder mit dem Initwert überschreibt!
// RTC_NOINIT_ATTR Werte bleiben erhalten und werden nur bei Power-Loss gelöscht.
RTC_NOINIT_ATTR uint32_t diagMinHeap;
RTC_NOINIT_ATTR uint32_t diagMinStack;
RTC_NOINIT_ATTR uint32_t diagHeapAtCrash;
RTC_NOINIT_ATTR uint32_t diagUptimeSeconds;
RTC_NOINIT_ATTR int8_t   diagLastRSSI;
RTC_NOINIT_ATTR uint8_t  diagWiFiConnected;
RTC_NOINIT_ATTR uint32_t diagBootCount;
RTC_NOINIT_ATTR uint32_t diagMagic;

#define DIAG_MAGIC_VALUE 0xC0DEBABE

void diagReset()
{
    diagMinHeap       = UINT32_MAX;
    diagMinStack      = UINT32_MAX;
    diagHeapAtCrash   = 0;
    diagUptimeSeconds = 0;
    diagLastRSSI      = 0;
    diagWiFiConnected = 0;
}

void diagLogBootInfo()
{
    diagBootCount++;

    // Power-On: Erste Boot, normale Meldung
    esp_reset_reason_t r = esp_reset_reason();
    if (r == ESP_RST_POWERON) {
        addLog("🔢 Boot-Zähler: 1 (Power-On)");
        return;
    }

    addLog("🔢 Boot-Zähler: " + String(diagBootCount));

    if (diagMinHeap != UINT32_MAX)
        addLog("📉 Heap-Minimum vor Restart: " + String(diagMinHeap / 1024) + " KB (" + String(diagMinHeap) + " B)");
    if (diagHeapAtCrash > 0)
        addLog("📉 Heap zuletzt: " + String(diagHeapAtCrash / 1024) + " KB");
    if (diagUptimeSeconds > 0)
        addLog("⏱️ Uptime vor Restart: " + String(diagUptimeSeconds) + "s (" + String(diagUptimeSeconds / 60) + " min)");
    if (diagMinStack != UINT32_MAX)
        addLog("📚 Stack-Minimum (Loop): " + String(diagMinStack) + " Bytes");
    if (diagLastRSSI != 0)
        addLog("📶 WiFi vor Restart: " + String(diagWiFiConnected ? "verbunden" : "GETRENNT")
             + ", RSSI: " + String(diagLastRSSI) + " dBm");
}

void diagUpdate()
{
    uint32_t minH = ESP.getMinFreeHeap();
    if (minH < diagMinHeap) diagMinHeap = minH;

    UBaseType_t stackLeft = uxTaskGetStackHighWaterMark(NULL);
    if ((uint32_t)stackLeft < diagMinStack) diagMinStack = (uint32_t)stackLeft;

    // Laufend sichern → nach Crash verfügbar
    diagUptimeSeconds = millis() / 1000;
    diagHeapAtCrash   = ESP.getFreeHeap();
    diagWiFiConnected = WiFi.isConnected() ? 1 : 0;
    diagLastRSSI      = (int8_t)WiFi.RSSI();
}

// ==================================================
// CRASH-BREADCRUMB (überlebt Watchdog-Reset in RTC-Memory)
// ==================================================
RTC_NOINIT_ATTR volatile uint8_t crashBreadcrumb;
RTC_NOINIT_ATTR uint8_t lastRestartSource;

void setCrumb(uint8_t c) { crashBreadcrumb = c; }

String crumbName(uint8_t c) {
    switch(c) {
        case CRUMB_IDLE:       return "Idle";
        case CRUMB_WEBSERVER:  return "WebServer.handleClient()";
        case CRUMB_MQTT:       return "MQTT";
        case CRUMB_WIFI:       return "WiFi-Watchdog";
        case CRUMB_NTP:        return "NTP-Sync";
        case CRUMB_MOTOR:      return "Motor/Taster";
        case CRUMB_LUX:        return "Lux-Sensor (I2C)";
        case CRUMB_I2C_HEALTH: return "Lux-Health/VEML-Reinit";
        case CRUMB_BME:        return "BME280/Relay/LittleFS";
        case CRUMB_LITTLEFS:   return "LittleFS-Write";
        case CRUMB_AUTOMATIK:  return "Automatik-Logik";
        case CRUMB_TELEGRAM:   return "Telegram (TLS)";
        case CRUMB_LIGHT:      return "Licht-Zustandsmaschine";
        default:               return "Unbekannt (" + String(c) + ")";
    }
}

String restartSourceName(uint8_t s) {
    switch(s) {
        case RESTART_VEML:     return "VEML7700 ausgefallen";
        case RESTART_OTA:      return "OTA-Update";
        case RESTART_MQTT_CMD: return "MQTT-Befehl";
        case RESTART_WEB_UI:   return "WebUI-Neustart";
        case RESTART_HEAP_LOW: return "Heap kritisch niedrig";
        default:               return "Unbekannt (Crash/WDT?)";
    }
}

// ==================================================
// HARDWARE-WATCHDOG (ESP32 Task-WDT)
// ==================================================
// Einfaches bewährtes System: Der Loop-Task wird direkt vom Task-WDT
// überwacht. wdogFeed() ruft esp_task_wdt_reset() auf.
// Timeout 60s → genug für WiFi-Reconnects und TLS-Verbindungen.
//
// KEIN separater Guardian-Task, KEIN esp_wifi_stop/start,
// KEIN IRAM_ATTR shutdown-handler → diese haben Deadlocks verursacht.
// ==================================================

#define WDOG_TIMEOUT_MS  120000UL   // 120s – WiFi-Stack braucht manchmal 60-90s zum Erholen

void tpl5110Init()
{
    // Bei echtem Power-On (Erstaufstarten) NOINIT-Variablen initialisieren.
    // Bei Software-Reset/WDT/Panic bleiben sie erhalten – das ist der Trick.
    esp_reset_reason_t bootReason = esp_reset_reason();
    if (bootReason == ESP_RST_POWERON || diagMagic != DIAG_MAGIC_VALUE) {
        diagMinHeap        = UINT32_MAX;
        diagMinStack       = UINT32_MAX;
        diagHeapAtCrash    = 0;
        diagUptimeSeconds  = 0;
        diagLastRSSI       = 0;
        diagWiFiConnected  = 0;
        diagBootCount      = 0;
        diagMagic          = DIAG_MAGIC_VALUE;
        crashBreadcrumb    = CRUMB_IDLE;
        lastRestartSource  = RESTART_UNKNOWN;
    }

#if ESP_IDF_VERSION_MAJOR >= 5
    esp_task_wdt_config_t cfg = {
        .timeout_ms     = WDOG_TIMEOUT_MS,
        .idle_core_mask = 0,
        .trigger_panic  = true,
    };
    if (esp_task_wdt_reconfigure(&cfg) == ESP_ERR_INVALID_STATE) {
        esp_task_wdt_init(&cfg);
    }
#else
    esp_task_wdt_init(WDOG_TIMEOUT_MS / 1000, true);
#endif
    esp_task_wdt_add(NULL);   // Loop-Task direkt überwachen
    Serial.printf("✅ Task-Watchdog aktiv (Timeout %lu ms)\n", WDOG_TIMEOUT_MS);
}

void wdogFeed()
{
    esp_task_wdt_reset();
}

// ==================================================
// I²C-BUS-MUTEX (siehe ausführlichen Kommentar in system.h)
// ==================================================
static SemaphoreHandle_t i2cMutex = nullptr;

void i2cMutexInit()
{
    if (!i2cMutex) i2cMutex = xSemaphoreCreateRecursiveMutex();
}

bool i2cTake(uint32_t timeoutMs)
{
    // Vor der Initialisierung (früher Boot, Single-Task) kein Schutz nötig.
    if (!i2cMutex) return true;
    return xSemaphoreTakeRecursive(i2cMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void i2cGive()
{
    if (i2cMutex) xSemaphoreGiveRecursive(i2cMutex);
}

// ==================================================
bool systemError()
{
    return errorWifi || errorMQTT || errorSensor;
}

void updateSystemHealth()
{
    errorWifi   = (WiFi.status() != WL_CONNECTED);
    errorMQTT   = (mqttSettings.enabled && !mqttClientConnected());
    errorSensor = (!hasVEML || vemlHardError);
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
static long simOffsetSeconds = 0;
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