#include "mqtt.h"
#include "storage.h"
#include "door.h"
#include "logic.h"
#include "motor.h"
#include "light.h"
#include "lux.h"
#include "system.h"
#include "logger.h"
#include <WiFi.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include "esp_task_wdt.h"


extern const char *FW_VERSION;

// ==================================================
// GLOBALE VARIABLEN
// ==================================================
WiFiClient   mqttWifi;
PubSubClient mqttClient(mqttWifi);

static unsigned long mqttLastConnectAttempt = 5000UL;  // 5s Boot-Verzögerung
static unsigned long mqttLastStatus         = 0;
static unsigned long mqttRetryInterval      = 30000UL;  // Start: 30s, steigt bis 5min
static bool          mqttWasConnected       = false;    // für Log-Spam-Vermeidung
static unsigned long mqttDisconnectAt       = 0;        // Zeitstempel des Disconnects (Logspam-Schutz)
static bool          mqttDisconnectLogged   = false;    // wurde der aktuelle Disconnect schon geloggt?
const  unsigned long MQTT_STATUS_INTERVAL_MS = 10000UL;

// ---- Crash-Schutz ----
// WiFi-Disconnect kann den TCP-Socket unter PubSubClient wegziehen.
// Wenn PubSubClient dann .loop(), .connected() oder .disconnect() aufruft,
// crasht lwIP (use-after-free auf dem PCB). Lösung: Flag das sofort gesetzt
// wird wenn WiFi verloren geht. Danach wird KEINE PubSubClient-Funktion
// mehr aufgerufen bis WiFi stabil zurück ist und wir .stop()+.connect() machen.
static volatile bool mqttSocketSafe = false;

// ==================================================
// HELPER
// ==================================================
static inline String t(const char *sub) { return String(mqttSettings.base) + "/" + sub; }
static inline void   mqttPublish(const String &topic, const String &payload, bool retained = false)
{
    if (!mqttSocketSafe) return;  // Crash-Schutz: kein Zugriff auf toten Socket
    mqttClient.publish(topic.c_str(), payload.c_str(), retained);
}

bool mqttClientConnected() { return mqttSocketSafe && mqttClient.connected(); }

void mqttSafeDisconnect()
{
    mqttSocketSafe   = false;
    mqttWasConnected = false;
    mqttWifi.stop();     // Nur fd schließen, kein Senden auf totem Socket
}

// ==================================================
// PUBLISH
// ==================================================
void mqttPublishAvailability(const char *state)
{
    if (!mqttSocketSafe) return;
    mqttPublish(t("tele/availability"), state, true);
}

void mqttPublishRaw(const String &topic, const String &payload)
{
    if (!mqttSocketSafe || !mqttClient.connected()) return;
    String fullTopic = String(mqttSettings.base) + "/" + topic;
    mqttClient.publish(fullTopic.c_str(), payload.c_str(), false);
}

void mqttPublishSettings(bool retained)
{
    JsonDocument doc;
    doc["openMode"]             = openMode;
    doc["closeMode"]            = closeMode;
    doc["openTime"]             = openTime;
    doc["closeTime"]            = closeTime;
    doc["openLightThreshold"]   = openLightThreshold;
    doc["closeLightThreshold"]  = closeLightThreshold;
    doc["lampPreOpen"]          = lampPreOpen;
    doc["lampPostOpen"]         = lampPostOpen;
    doc["lampPreClose"]         = lampPreClose;
    doc["lampPostClose"]        = lampPostClose;
    String out; serializeJson(doc, out);
    mqttPublish(t("tele/settings"), out, retained);
}

void mqttPublishStatus()
{
    DateTime nowDT = nowRTC();
    JsonDocument doc;
    bool timeValid    = rtcOk || (nowDT.year() > 2000);
    doc["time"]       = timeValid ? nowDT.timestamp(DateTime::TIMESTAMP_TIME) : "--:--:--";
    doc["date"]       = timeValid ? (String(nowDT.day()) + "." + String(nowDT.month()) + "." + String(nowDT.year())) : "-";
    doc["door"]       = doorOpen ? "Offen" : "Geschlossen";
    doc["moving"]     = (motorState != MOTOR_STOPPED) ? "1" : "0";

    // Türposition in % berechnen
    int doorPct = 0;
    if (doorOpen && motorState == MOTOR_STOPPED) {
        doorPct = 100;
    } else if (!doorOpen && motorState == MOTOR_STOPPED) {
        doorPct = 0;
    } else if (motorState == MOTOR_OPENING && openPosition > 0) {
        unsigned long elapsed = millis() - motorStartedAt;
        doorPct = (int)constrain((long)elapsed * 100 / openPosition, 0, 99);
    } else if (motorState == MOTOR_CLOSING && closePosition > 0) {
        unsigned long elapsed = millis() - motorStartedAt;
        doorPct = (int)constrain(100 - (long)elapsed * 100 / closePosition, 1, 100);
    }
    doc["door_pct"]   = doorPct;

    doc["light"]      = isfinite(lux) ? String(lux, 1) : "n/a";
    doc["sensor"]     = (hasVEML && !vemlHardError) ? "VEML7700 OK" : "Lichtsensor Fehler";
    doc["lightState"] = lightActive ? "An" : "Aus";
    doc["stallLight"] = stallLightActive ? "An" : "Aus";
    doc["fw"]         = FW_VERSION;
    doc["statOpen"]   = statOpenCount;
    doc["statClose"]  = statCloseCount;
    doc["statMinOpen"]= (int)(statOpenDurationMs / 60000UL);
    // Klappe 2
    doc["door2"]      = door2Open ? "Offen" : "Geschlossen";
    doc["moving2"]    = (motor2State != MOTOR_STOPPED) ? "1" : "0";
    String out; serializeJson(doc, out);
    mqttPublish(t("tele/status"), out);
}

void mqttPublishLog(const String &line)
{
    mqttPublish(t("tele/log"), line);
}

// ==================================================
// SETTINGS APPLY
// ==================================================
bool applySettingsFromJson(const JsonDocument &doc, String &err)
{
    Settings s = settings;
    if (!doc["openMode"].isNull())          { strncpy(s.openMode,  doc["openMode"],  5);  s.openMode[5]  = '\0'; }
    if (!doc["closeMode"].isNull())         { strncpy(s.closeMode, doc["closeMode"], 5);  s.closeMode[5] = '\0'; }
    if (!doc["openTime"].isNull())          { strncpy(s.openTime,  doc["openTime"],  5);  s.openTime[5]  = '\0'; }
    if (!doc["closeTime"].isNull())         { strncpy(s.closeTime, doc["closeTime"], 5);  s.closeTime[5] = '\0'; }
    if (!doc["openLightThreshold"].isNull())  s.openLightThreshold  = doc["openLightThreshold"].as<int>();
    if (!doc["closeLightThreshold"].isNull()) s.closeLightThreshold = doc["closeLightThreshold"].as<int>();
    if (!doc["lampPreOpen"].isNull())   s.lampPreOpen   = doc["lampPreOpen"].as<int>();
    if (!doc["lampPostOpen"].isNull())  s.lampPostOpen  = doc["lampPostOpen"].as<int>();
    if (!doc["lampPreClose"].isNull())  s.lampPreClose  = doc["lampPreClose"].as<int>();
    if (!doc["lampPostClose"].isNull()) s.lampPostClose = doc["lampPostClose"].as<int>();

    if (strcmp(s.openMode, "light") == 0 && strcmp(s.closeMode, "light") == 0)
    {
        if (s.openLightThreshold <= s.closeLightThreshold)
        {
            err = "Fehler: Öffnen-Lux muss größer sein als Schließen-Lux!";
            return false;
        }
    }
    settings = s;
    saveSettings();
    applySettingsToRam();
    return true;
}

// ==================================================
// CALLBACK
// ==================================================
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    String top = String(topic);
    String pay; pay.reserve(length + 1);
    for (unsigned int i = 0; i < length; i++) pay += (char)payload[i];
    String cmd = pay; cmd.trim(); cmd.toUpperCase();

    // DOOR
    if (top == t("cmnd/door"))
    {
        if (otaInProgress || ioSafeState) { addLog("MQTT: Motor gesperrt"); return; }
        if (cmd == "OPEN" && !doorOpen && motorState == MOTOR_STOPPED)
        {
            doorPhase = PHASE_OPENING; motorReason = "manuell/MQTT";
            startMotorOpen(openPosition); actionLock = true;
            preLightCloseDone = false; preLightOpenDone = false;
            manualOverrideUntil = millis() + 900000UL;  // 15 Minuten
            lightBelowSince = 0;
            nightLock = false;
            addLog("Öffnung gestartet (MQTT) – Automatik pausiert 15 min");
        }
        else if (cmd == "CLOSE" && doorOpen && motorState == MOTOR_STOPPED)
        {
            doorPhase = PHASE_CLOSING; motorReason = "manuell/MQTT";
            startMotorClose(closePosition); actionLock = true;
            preLightOpenDone = false;
            manualOverrideUntil = millis() + 300000UL;
            addLog("Schließvorgang gestartet (MQTT)");
        }
        else if (cmd == "STOP" && motorState != MOTOR_STOPPED)
        {
            motorStop(); motorState = MOTOR_STOPPED;
            motorReason = "Stop/MQTT";
            doorPhase   = doorOpen ? PHASE_OPEN : PHASE_IDLE;
            addLog("Motor gestoppt (MQTT)");
        }
        else if (cmd == "TOGGLE")
        {
            if (motorState != MOTOR_STOPPED)
            {
                motorStop(); motorState = MOTOR_STOPPED;
                doorPhase = doorOpen ? PHASE_OPEN : PHASE_IDLE;
                addLog("Motor gestoppt (MQTT/Toggle)");
            }
            else if (doorOpen)
            {
                doorPhase = PHASE_CLOSING; motorReason = "manuell/MQTT";
                startMotorClose(closePosition); actionLock = true;
                manualOverrideUntil = millis() + 300000UL;
                addLog("Schließvorgang gestartet (MQTT/Toggle)");
            }
            else
            {
                doorPhase = PHASE_OPENING; motorReason = "manuell/MQTT";
                startMotorOpen(openPosition); actionLock = true;
                preLightCloseDone = false; preLightOpenDone = false;
                manualOverrideUntil = millis() + 300000UL;
                addLog("Öffnung gestartet (MQTT/Toggle)");
            }
        }
        mqttPublishStatus(); return;
    }

    // DOOR2
    if (top == t("cmnd/door2"))
    {
        if (otaInProgress || ioSafeState) { addLog("MQTT: Motor2 gesperrt"); return; }
        if (cmd == "OPEN" && !door2Open && !isAnyMotorRunning())
        {
            door2Phase = PHASE_OPENING; motor2Reason = "manuell/MQTT";
            startMotor2Open(door2OpenPosition); actionLock2 = true;
            addLog("Klappe2 Öffnung (MQTT)");
        }
        else if (cmd == "CLOSE" && door2Open && !isAnyMotorRunning())
        {
            door2Phase = PHASE_CLOSING; motor2Reason = "manuell/MQTT";
            startMotor2Close(door2ClosePosition); actionLock2 = true;
            addLog("Klappe2 Schließvorgang (MQTT)");
        }
        else if (cmd == "STOP" && motor2State != MOTOR_STOPPED)
        {
            motor2Stop(); motor2State = MOTOR_STOPPED;
            motor2Reason = "Stop/MQTT";
            door2Phase   = door2Open ? PHASE_OPEN : PHASE_IDLE;
            addLog("Klappe2 Motor gestoppt (MQTT)");
        }
        else if (cmd == "TOGGLE")
        {
            if (motor2State != MOTOR_STOPPED)
            {
                motor2Stop(); motor2State = MOTOR_STOPPED;
                door2Phase = door2Open ? PHASE_OPEN : PHASE_IDLE;
                addLog("Klappe2 Motor gestoppt (MQTT/Toggle)");
            }
            else if (!isAnyMotorRunning())
            {
                if (door2Open)
                {
                    door2Phase = PHASE_CLOSING; motor2Reason = "manuell/MQTT";
                    startMotor2Close(door2ClosePosition); actionLock2 = true;
                    addLog("Klappe2 Schließvorgang (MQTT/Toggle)");
                }
                else
                {
                    door2Phase = PHASE_OPENING; motor2Reason = "manuell/MQTT";
                    startMotor2Open(door2OpenPosition); actionLock2 = true;
                    addLog("Klappe2 Öffnung (MQTT/Toggle)");
                }
            }
        }
        mqttPublishStatus(); return;
    }

    // LIGHT
    if (top == t("cmnd/light"))
    {
        if (cmd == "ON")     { manualLightActive = true;  lightOn();  lightActive = true;  addLog("Locklicht AN (MQTT)"); }
        if (cmd == "OFF")    { manualLightActive = false; lightOff(); lightActive = false; addLog("Locklicht AUS (MQTT)"); }
        if (cmd == "TOGGLE")
        {
            if (manualLightActive) { manualLightActive = false; lightOff(); lightActive = false; addLog("Locklicht AUS (MQTT/Toggle)"); }
            else                   { manualLightActive = true;  lightOn();  lightActive = true;  addLog("Locklicht AN (MQTT/Toggle)"); }
        }
        mqttPublishStatus(); return;
    }

    // STALLLIGHT
    if (top == t("cmnd/stalllight"))
    {
        if (cmd == "ON") stallLightOn();
        else if (cmd == "OFF") stallLightOff();
        else if (cmd == "TOGGLE") { if (stallLightActive) stallLightOff(); else stallLightOn(); }
        mqttPublishStatus(); return;
    }

    // SETTINGS
    if (top == t("cmnd/settings"))
    {
        JsonDocument d;
        if (deserializeJson(d, pay)) { addLog("MQTT Settings JSON Fehler"); return; }
        String err;
        if (applySettingsFromJson(d, err)) { addLog("MQTT Settings übernommen"); mqttPublishSettings(true); mqttPublishStatus(); }
        else addLog("MQTT Settings FEHLER: " + err);
        return;
    }

    // GET
    if (top == t("cmnd/get"))
    {
        if (cmd == "STATUS")    mqttPublishStatus();
        else if (cmd == "SETTINGS") mqttPublishSettings(true);
        else if (cmd == "LOG")
        {
            for (int i = 0; i < LOG_SIZE; i++)
            {
                int idx = (logIndex + i) % LOG_SIZE;
                if (logbook[idx].length() > 0) mqttPublishLog(logbook[idx]);
                if (i % 10 == 0) { yield(); esp_task_wdt_reset(); }
            }
        }
        return;
    }

    // REBOOT
    if (top == t("cmnd/reboot") && cmd == "NOW")
    {
        addLog("MQTT: Reboot…");
        mqttPublishAvailability("offline");
        lastRestartSource = RESTART_MQTT_CMD;
        delay(100);
        ESP.restart();
    }
}

// ==================================================
// SETUP + LOOP
// ==================================================
void mqttSubscribeAll() { mqttClient.subscribe(t("cmnd/#").c_str()); }

void mqttSetup()
{
    mqttClient.setServer(mqttSettings.host, mqttSettings.port);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(1024);
    mqttClient.setKeepAlive(30);     // 30s – sendet alle 30s einen Ping. Hält NAT-Tabelle und Broker-Idle-Timer frisch
    mqttClient.setSocketTimeout(3);  // 3s – Kompromiss zwischen Blockade und Zuverlässigkeit

    // WiFi-Event-Handler: wird SOFORT aufgerufen wenn WiFi wegfällt,
    // BEVOR der nächste Loop-Durchlauf PubSubClient anfassen kann.
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        mqttSocketSafe = false;  // Ab jetzt KEINE PubSubClient-Aufrufe mehr!
    }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
}

static void mqttEnsureConnected()
{
    // Nicht verbinden wenn deaktiviert oder kein gültiger Host
    if (!mqttSettings.enabled) return;
    if (strlen(mqttSettings.host) < 4) return;

    // WiFi muss stabil stehen bevor wir irgendwas machen
    if (!WiFi.isConnected()) return;

    // Wenn Socket safe ist: normaler Connected-Check erlaubt
    if (mqttSocketSafe && mqttClient.connected()) {
        if (!mqttWasConnected) {
            // Nur loggen wenn der Disconnect länger als 5 min war (echter Ausfall),
            // sonst kurze Broker-Glitches nicht ins Log spammen
            if (mqttDisconnectAt > 0 && (millis() - mqttDisconnectAt) > 300000UL) {
                unsigned long downSec = (millis() - mqttDisconnectAt) / 1000;
                addLog("MQTT verbunden (war " + String(downSec) + "s offline)");
            }
            mqttWasConnected = true;
            mqttDisconnectAt = 0;
            mqttRetryInterval = 30000UL;
        }
        return;
    }

    // Gerade verloren? Zeitstempel merken (nicht direkt loggen)
    if (mqttWasConnected) {
        mqttWasConnected = false;
        mqttDisconnectAt = millis();
    }

    // Wenn schon länger als 5 min offline und noch nicht geloggt → echter Ausfall
    if (!mqttDisconnectLogged && mqttDisconnectAt > 0 &&
        (millis() - mqttDisconnectAt) > 300000UL) {
        addLog("MQTT Verbindung verloren");
        mqttDisconnectLogged = true;
    }
    if (mqttSocketSafe && mqttClient.connected()) mqttDisconnectLogged = false;

    // WiFi-Signal zu schwach? Nicht versuchen
    int rssi = WiFi.RSSI();
    if (rssi < -85 && rssi != 0) return;

    unsigned long nowMs = millis();
    if (nowMs - mqttLastConnectAttempt < mqttRetryInterval) return;
    mqttLastConnectAttempt = nowMs;

    // ===== SICHERER RECONNECT =====
    // 1. WiFiClient sauber schließen (fd close, kein Senden auf totem Socket)
    mqttWifi.stop();
    delay(10);  // lwIP Socket-Cleanup Zeit geben

    // 2. Prüfen ob WiFi noch da ist (könnte während stop() verloren gegangen sein)
    if (!WiFi.isConnected()) return;

    wdogFeed();

    // 3. Frischer Connect-Versuch
    String willTopic = t("tele/availability");
    bool ok = (strlen(mqttSettings.user) > 0)
        ? mqttClient.connect(mqttSettings.clientId, mqttSettings.user, mqttSettings.pass, willTopic.c_str(), 0, true, "offline")
        : mqttClient.connect(mqttSettings.clientId, willTopic.c_str(), 0, true, "offline");

    wdogFeed();

    if (ok)
    {
        mqttSocketSafe   = true;   // Socket ist jetzt gültig → PubSubClient-Zugriff erlaubt
        mqttWasConnected = true;
        mqttRetryInterval = 30000UL;
        addLog("MQTT verbunden");
        mqttPublishAvailability("online");
        mqttSubscribeAll();
        mqttPublishSettings(true);
        mqttPublishStatus();
    }
    else
    {
        mqttSocketSafe = false;    // Socket ungültig → kein Zugriff
        mqttWifi.stop();           // Aufräumen
        // Exponentielles Backoff: 30s → 60s → 120s → 300s (max 5 min)
        mqttRetryInterval = min(mqttRetryInterval * 2, 300000UL);
        Serial.printf("[MQTT] Verbindung fehlgeschlagen – nächster Versuch in %lus\n",
                      mqttRetryInterval / 1000);
    }
}

void mqttLoop()
{
    if (!mqttSettings.enabled)              return;
    if (strlen(mqttSettings.host) < 4)      return;

    // WiFi weg oder Socket ungültig? 
    // NICHT mqttClient.disconnect() aufrufen – das sendet auf dem toten Socket → Crash!
    // Nur Flag setzen und warten bis WiFi zurück ist.
    if (!WiFi.isConnected()) {
        if (mqttSocketSafe) {
            mqttSocketSafe = false;
            // mqttWifi.stop() hier NICHT aufrufen – Socket könnte schon vom
            // WiFi-Stack freigegeben sein. Wird in mqttEnsureConnected()
            // sicher aufgeräumt bevor der nächste connect() kommt.
        }
        if (mqttWasConnected) {
            mqttWasConnected = false;
            addLog("MQTT Verbindung verloren");
        }
        return;
    }

    // Socket nicht sicher? Reconnect-Logik kümmert sich darum
    if (!mqttSocketSafe) {
        mqttEnsureConnected();
        return;
    }

    mqttEnsureConnected();
    if (!mqttSocketSafe || !mqttClient.connected()) return;
    mqttClient.loop();
    unsigned long nowMs = millis();
    if (nowMs - mqttLastStatus >= MQTT_STATUS_INTERVAL_MS)
    {
        mqttLastStatus = nowMs;
        mqttPublishStatus();
    }
}