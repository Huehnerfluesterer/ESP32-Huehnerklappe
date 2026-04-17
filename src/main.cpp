// ==========================
// FIRMWARE VERSION
// ==========================
const char *FW_VERSION = "3.0.16";

// ==========================
// INCLUDES
// ==========================
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <WebServer.h>
#include <RTClib.h>
#include <EEPROM.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include "esp_task_wdt.h"

#include "pins.h"
#include "types.h"
#include "storage.h"
#include "system.h"
#include "logger.h"
#include "lux.h"
#include "light.h"
#include "motor.h"
#include "door.h"
#include "logic.h"
#include "mqtt.h"
#include "telegram.h"
#include "wlan.h"
#include "bme.h"
#include "relay.h"
#include "espnow_dispatch.h"
#include "web/web.h"
#include "icons.h"       // icon192_png / icon512_png PROGMEM-Arrays
#include "statusled.h"   // WS2812 Status-LED (IO48)

// ==========================
// GLOBALE OBJEKTE
// ==========================
WebServer server(80);
unsigned long bootTime    = 0;
unsigned long lastLogicRun = 0;
const unsigned long LOGIC_INTERVAL = 50;  // 50ms = 20× pro Sekunde (war 200ms)

// Lux-Zustand für Loop
static unsigned long lastLuxRead   = 0;
static unsigned long lastTrendUpdate = 0;
static float         luxFiltered   = 0.0f;
static bool          luxInitDone   = false;

// ACS712 – Variablen und Logik in motor.cpp

int stallLightMinutes = 1;   // in light.cpp referenziert

// ==========================
// SETUP
// ==========================
void setup()
{

    Serial.begin(115200);
    Serial.println("\n🐔 Hühnerklappe – FW " + String(FW_VERSION));
    tpl5110Init();

    // ===== EEPROM (muss zuerst) =====
    loggerInit();   // LittleFS mounten + alte Logs laden (vor storageInit!)
    storageInit();

    // ===== EINSTELLUNGEN LADEN =====
    loadMqttSettings();
    loadTelegramSettings();
    loadSettings();
    loadDoorState();
    loadMotorPositions();
    loadTheme();
    loadLimitSwitchSetting();
    loadBlockadeSettings();
    loadBmeSource();
    loadRelaySettings();
    loadRgbSettings();
    loadStallLightSettings();
    loadCloseDelay();

    // ===== GPIO =====
    pinMode(MOTOR_IN1,          OUTPUT); digitalWrite(MOTOR_IN1, LOW);
    pinMode(MOTOR_IN2,          OUTPUT); digitalWrite(MOTOR_IN2, LOW);
    pinMode(RELAIS_PIN,         OUTPUT); digitalWrite(RELAIS_PIN, RELAY_OFF);
    pinMode(STALLLIGHT_RELAY_PIN,OUTPUT);digitalWrite(STALLLIGHT_RELAY_PIN, STALLLIGHT_OFF);
    pinMode(BUTTON_PIN,         INPUT_PULLUP);
    pinMode(STALL_BUTTON_PIN,   INPUT_PULLUP);
    pinMode(RED_BUTTON_PIN,     INPUT_PULLUP);
    pinMode(LIMIT_OPEN_PIN,     INPUT_PULLUP);
    pinMode(LIMIT_CLOSE_PIN,    INPUT_PULLUP);

    // ===== RGB + MOTOR =====
    lightInit();
    statusLedInit();   // Status-LED (WS2812 IO48) initialisieren
    motorInit();
    digitalWrite(MOTOR_IN1, LOW);
    digitalWrite(MOTOR_IN2, LOW);
    ledcWrite(3, 0);

    // ===== I2C + RTC + VEML =====
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);   // 100 kHz – stabiler als 30 kHz
    Wire.setTimeOut(20);     // 20ms Timeout (war 50ms)
    rtcOk = rtc.begin();
    if (!rtcOk) Serial.println("⚠️ RTC DS3231 nicht gefunden");
    luxInit();
    // bmeInit() nach WiFi – ESP-NOW benötigt initialisierten WiFi-Stack

    // ===== WIFI + NTP =====
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("Huehnerklappe-ESP32");
    wifiConnectNonBlocking();

    // Warten bis WiFi verbunden – ESP-NOW braucht den korrekten Kanal
    // Max 5s warten, dann trotzdem weitermachen
    {
        unsigned long t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
            delay(50);
            esp_task_wdt_reset();   // WDT während WLAN-Connect am Leben halten
            statusLedUpdate();   // Gelbes Blinken während WiFi-Connect sichtbar machen
        }
        if (WiFi.status() == WL_CONNECTED)
            Serial.printf("✅ WLAN verbunden – Kanal %d  IP: %s\n", WiFi.channel(), WiFi.localIP().toString().c_str());
        else
            Serial.println("⚠️ WLAN noch nicht verbunden – ESP-NOW Kanal evtl. falsch");
    }

    bmeInit();    // nach WiFi verbunden – ESP-NOW braucht korrekten Kanal
    relayInit();  // nach bmeInit() – nutzt ggf. denselben ESP-NOW Stack
    espnowDispatcherInit();  // einziger recv_cb – leitet an bme/relay weiter

    configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
    {
        struct tm ti;
        static bool rtcSynced = false;
        if (!rtcSynced && getLocalTime(&ti, 2000))
        {
            if (rtcOk) rtc.adjust(DateTime(ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec));
            Serial.println("✅ RTC per NTP synchronisiert");
            rtcSynced = true;
        }
    }

    // ===== TÜRZUSTAND =====
    if (doorOpen) { doorPhase = PHASE_OPEN; doorOpenedAt = millis(); }
    else          { doorPhase = PHASE_IDLE; }
    preLightOpenDone = false;
    lightAboveSince  = 0;
    lightBelowSince  = 0;

    // ===== MQTT =====
    mqttSetup();

    // ===== TELEGRAM =====
    telegramInit();

    // ===== WEBSERVER ROUTEN =====
    server.on("/",              handleRoot);
    server.on("/status",        handleStatus);
    server.on("/settings",      handleSettings);
    server.on("/save-open",  HTTP_POST, handleSaveOpen);
    server.on("/save-close", HTTP_POST, handleSaveClose);
    server.on("/advanced",   HTTP_GET,  handleAdvanced);
    server.on("/espnow",     HTTP_GET,  handleEspNow);
    server.on("/rgb",        HTTP_GET,  handleRgb);
    server.on("/save-rgb",   HTTP_POST, []() {
        rgbColorR    = (uint8_t)constrain(server.arg("r").toInt(),  0, 255);
        rgbColorG    = (uint8_t)constrain(server.arg("g").toInt(),  0, 255);
        rgbColorB    = (uint8_t)constrain(server.arg("b").toInt(),  0, 255);
        rgbColorW    = (uint8_t)constrain(server.arg("w").toInt(),  0, 255);
        rgbBrightness= (uint8_t)constrain(server.arg("br").toInt(), 1, 255);
        saveRgbSettings();
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
    });
    server.on("/fw",         HTTP_GET,  handleFw);
    server.on("/blockade",   HTTP_GET,  handleBlockade);
    server.on("/save-blockade", HTTP_POST, []() {
        if (otaInProgress || ioSafeState) { server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(503, "text/plain", "OTA aktiv"); return; }
        blockadeEnabled    = server.arg("enabled")   == "1";
        blockadeThresholdA = server.arg("threshold").toFloat();
        if (blockadeThresholdA < 0.5f || blockadeThresholdA > 10.0f) blockadeThresholdA = 2.0f;
        saveBlockadeSettings();
        addLog(String("Blockade: ") + (blockadeEnabled ? "aktiv" : "deaktiviert") +
               ", Schwelle=" + String(blockadeThresholdA, 1) + "A");
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
    });
    server.on("/blockade-live", HTTP_GET, []() {
        // 100 Samples für stabilen Mittelwert (ESP32 ADC rauscht stark)
        const int S = 100; long sum = 0;
        for (int i = 0; i < S; i++) sum += analogRead(ACS712_PIN);
        float vMeas   = (sum / S) * (3.3f / 4095.0f);
#if ACS712_HAS_DIVIDER
        float vSensor = vMeas / (20.0f / 30.0f);
#else
        float vSensor = vMeas;
#endif
        float amps    = fabsf((vSensor - ACS712_ZERO_V) / (ACS712_MV_PER_A / 1000.0f));
        // Gleitender Mittelwert → Display ruhig halten
        static float filtered = 0.0f;
        static bool  firstRun = true;
        if (firstRun) { filtered = amps; firstRun = false; }
        else          { filtered = filtered * 0.6f + amps * 0.4f; }
        // Sensor-Warnung NUR im Stillstand – beim laufenden Motor immer Rohwert zeigen
        if (filtered > 8.0f && motorState == MOTOR_STOPPED) {
            server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "-- (kein Sensor?)");
            return;
        }
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", String(filtered, 2));
    });
    server.on("/blockade-peak", HTTP_GET, []() {
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", String(peakCurrentA, 2));
    });
    server.on("/blockade-baseline", HTTP_GET, []() {
        server.client().setNoDelay(true);
        server.sendHeader("Connection","close");
        if (currentCalibrated)
            server.send(200, "text/plain", String(currentBaseline, 2));
        else
            server.send(200, "text/plain", "--");
    });
    server.on("/blockade-peak-reset", HTTP_POST, []() {
        peakCurrentA = 0.0f;
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
    });
    // Kombinierter Endpoint – ersetzt 3 einzelne Fetches (iOS Performance)
    server.on("/blockade-all", HTTP_GET, []() {
        const int S = 100; long sum = 0;
        for (int i = 0; i < S; i++) sum += analogRead(ACS712_PIN);
        float vMeas   = (sum / S) * (3.3f / 4095.0f);
#if ACS712_HAS_DIVIDER
        float vSensor = vMeas / (20.0f / 30.0f);
#else
        float vSensor = vMeas;
#endif
        float amps    = fabsf((vSensor - ACS712_ZERO_V) / (ACS712_MV_PER_A / 1000.0f));
        static float filtered = 0.0f;
        static bool  firstRun = true;
        if (firstRun) { filtered = amps; firstRun = false; }
        else          { filtered = filtered * 0.6f + amps * 0.4f; }
        String live = (filtered > 8.0f && motorState == MOTOR_STOPPED) ? "--" : String(filtered, 2);
        String base = currentCalibrated ? String(currentBaseline, 2) : "--";
        String out = "{\"live\":\"" + live + "\",\"peak\":\"" + String(peakCurrentA, 2) + "\",\"base\":\"" + base + "\"}";
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "application/json", out);
    });
    server.on("/save-bme-source", HTTP_POST, []() {
        BmeSource newSrc = (server.arg("source") == "1") ? BME_SOURCE_ESPNOW : BME_SOURCE_LOCAL;
        bmeSetSource(newSrc);
        saveBmeSource();
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
    });

    server.on("/save-relay", HTTP_POST, []() {
        relayEnabled = (server.arg("enabled") == "1");
        // MAC parsen: "AA:BB:CC:DD:EE:FF"
        String mac = server.arg("mac");
        if (mac.length() == 17) {
            for (int i = 0; i < 6; i++)
                relayMac[i] = strtoul(mac.substring(i*3, i*3+2).c_str(), nullptr, 16);
        }
        saveRelaySettings();
        relayReset();
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
    });

    server.on("/bme-mac", HTTP_GET, []() {
        String info = WiFi.macAddress() + "|" + String(WiFi.channel());
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", info);
    });
    server.on("/espnow-status", HTTP_GET, []() {
        JsonDocument doc;
        // BME280
        doc["bmeEnabled"]  = (bmeSource == BME_SOURCE_ESPNOW);
        doc["bmeOk"]       = bmeOk;
        if (bmeLastReceived > 0) {
            unsigned long ago = (millis() - bmeLastReceived) / 1000;
            String s;
            if      (ago < 60)   s = "vor " + String(ago) + " s";
            else if (ago < 3600) s = "vor " + String(ago/60) + " min";
            else                 s = "vor " + String(ago/3600) + " h";
            doc["bmeLastSeen"] = s;
            char mac[18];
            snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                bmeLastSenderMac[0], bmeLastSenderMac[1], bmeLastSenderMac[2],
                bmeLastSenderMac[3], bmeLastSenderMac[4], bmeLastSenderMac[5]);
            doc["bmeSenderMac"] = mac;
        } else {
            doc["bmeLastSeen"]  = "–";
            doc["bmeSenderMac"] = "–";
        }
        // Relais
        doc["relayEnabled"] = relayEnabled;
        if (relayMacValid()) {
            char rmac[18];
            snprintf(rmac, sizeof(rmac), "%02X:%02X:%02X:%02X:%02X:%02X",
                relayMac[0], relayMac[1], relayMac[2],
                relayMac[3], relayMac[4], relayMac[5]);
            doc["relayMac"] = rmac;
        } else {
            doc["relayMac"] = "";
        }
        // Online wenn Heartbeat innerhalb der letzten 90s
        bool relayOnline = relayEnabled && relayMacValid() &&
                           relayLastHeartbeat > 0 &&
                           (millis() - relayLastHeartbeat < 90000UL);
        doc["relayOnline"] = relayOnline;
        if (relayLastHeartbeat > 0) {
            unsigned long ago = (millis() - relayLastHeartbeat) / 1000;
            String s;
            if      (ago < 60)   s = "vor " + String(ago) + " s";
            else if (ago < 3600) s = "vor " + String(ago/60) + " min";
            else                 s = "vor " + String(ago/3600) + " h";
            doc["relayLastSeen"] = s;
        } else {
            doc["relayLastSeen"] = "–";
        }
        String out; serializeJson(doc, out);
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "application/json", out);
    });
    server.on("/systemtest", HTTP_GET,  handleSelftest);
    server.on("/mqtt",       HTTP_GET,  handleMqtt);
    server.on("/save-mqtt",  HTTP_POST, handleSaveMqtt);
    server.on("/telegram",      HTTP_GET,  handleTelegram);
    server.on("/save-telegram", HTTP_POST, handleSaveTelegram);
    server.on("/telegram-test", HTTP_POST, handleTelegramTest);
    server.on("/calibration",   handleCalibration);
    server.on("/learn",         handleLearn);
    server.on("/learn-page",    handleLearnPage);
    server.on("/learn-start", HTTP_POST, handleLearn);
    server.on("/log",           handleLogbook);
server.on("/save-stalllight", HTTP_POST, []() {
    stallLightAutoOff = server.arg("autooff") == "1";
    int min = server.arg("minutes").toInt();
    stallLightMinutes = constrain(min, 1, 120);
    saveStallLightSettings();
    server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
});
    server.on("/mini",   []() { server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK"); });

    server.on("/log/clear", HTTP_POST, []() {
        if (otaInProgress || ioSafeState) { server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(503, "text/plain", "OTA aktiv"); return; }
        clearLogbook(); addLog("Logbuch manuell gelöscht");
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
    });

    server.on("/log/download", HTTP_GET, []() {
        if (!LittleFS.exists(LOG_FILE)) {
            server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(404, "text/plain", "Kein Logfile vorhanden");
            return;
        }
        File f = LittleFS.open(LOG_FILE, "r");
        server.sendHeader("Connection","close"); server.streamFile(f, "text/plain");
        f.close();
    });

    server.on("/set-theme", HTTP_POST, []() {
        String t = server.arg("theme");
        if (t == "dark" || t == "light" || t == "auto") saveTheme(t);
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
    });

    server.on("/set-limit-switches", HTTP_POST, []() {
        if (server.hasArg("enabled")) {
            useLimitSwitches = server.arg("enabled") == "1";
            EEPROM.put(EEPROM_ADDR_LIMIT_SW, useLimitSwitches); EEPROM.commit();
            addLog(String("Endschalter ") + (useLimitSwitches ? "aktiviert" : "deaktiviert"));
        }
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
    });

    server.on("/door", []() {
        if (otaInProgress || ioSafeState) { server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(503, "text/plain", "OTA aktiv"); return; }
        if (motorState != MOTOR_STOPPED) {
            motorStop(); motorState = MOTOR_STOPPED; motorReason = "Stop/Manuell";
            doorPhase = doorOpen ? PHASE_OPEN : PHASE_IDLE;
            addLog("Motor gestoppt"); server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "STOP"); return;
        }
        if (doorOpen) {
            doorPhase = PHASE_CLOSING; motorReason = "manuell/Web (Toggle)";
            startMotorClose(closePosition); actionLock = true;
            preLightOpenDone = false; manualOverrideUntil = millis() + 300000UL;
            server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "Closing"); addLog("Schließvorgang gestartet (Toggle)");
        } else {
            doorPhase = PHASE_OPENING; motorReason = "manuell/Web (Toggle)";
            startMotorOpen(openPosition); actionLock = true;
            preLightCloseDone = false; preLightOpenDone = false;
            manualOverrideUntil = millis() + 900000UL;  // 15 Minuten
            lightBelowSince = 0;
            nightLock = false;
            server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "Opening"); addLog("Öffnung gestartet (Toggle) – Automatik pausiert 15 min");
        }
    });

    server.on("/open", []() {
        if (otaInProgress || ioSafeState) { server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(503, "text/plain", "OTA aktiv"); return; }
        if (doorOpen) { server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "Already open"); return; }
        if (motorState != MOTOR_STOPPED) { server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "Motor running"); return; }
        doorPhase = PHASE_OPENING; motorReason = "manuell/Web";
        startMotorOpen(openPosition); actionLock = true;
        preLightCloseDone = false; preLightOpenDone = false; manualOverrideUntil = millis() + 300000UL;
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "Opening");
    });

    server.on("/close", []() {
        if (otaInProgress || ioSafeState) { server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(503, "text/plain", "OTA aktiv"); return; }
        if (!doorOpen) { server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "Already closed"); return; }
        if (motorState != MOTOR_STOPPED) { server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "Motor running"); return; }
        doorPhase = PHASE_CLOSING; motorReason = "manuell/Web";
        startMotorClose(closePosition); actionLock = true;
        preLightOpenDone = false; manualOverrideUntil = millis() + 300000UL;
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "Closing");
    });

    server.on("/light", []() {
        if (manualLightActive) { manualLightActive = false; lightOff(); lightActive = false; addLog("Locklicht manuell AUS"); server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OFF"); }
        else { manualLightActive = true; lightOn(); lightActive = true; addLog("Locklicht manuell AN"); server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "ON"); }
    });

    server.on("/stalllight", []() {
        if (stallLightActive) { stallLightOff(); server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OFF"); }
        else { stallLightOn(); server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "ON"); }
    });

    server.on("/rgbred", []() {
        if (rgbRedActive) { rgbRedOff(); server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OFF"); }
        else { rgbRedOn(); server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "ON"); }
    });

    server.on("/clear-override", HTTP_POST, []() {
        manualOverrideUntil = 0;
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
    });

    server.on("/live-rgb", HTTP_POST, []() {
        if (server.hasArg("r") && server.hasArg("br")) {
            uint8_t r  = constrain(server.arg("r").toInt(),  0, 255);
            uint8_t g  = constrain(server.arg("g").toInt(),  0, 255);
            uint8_t b  = constrain(server.arg("b").toInt(),  0, 255);
            uint8_t w  = constrain(server.arg("w").toInt(),  0, 255);
            uint8_t br = constrain(server.arg("br").toInt(), 1, 255);
            // Nur anzeigen wenn Licht gerade aktiv (nicht Grundeinstellungen überschreiben)
            lightPreview(r, g, b, br, w);
        }
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
    });

    server.on("/red-brightness", HTTP_POST, []() {
        if (server.hasArg("v")) {
            int v = constrain(server.arg("v").toInt(), 1, 255);
            rgbRedSetBrightness((uint8_t)v);
            server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
        } else { server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(400, "text/plain", "missing v"); }
    });

    server.on("/motor/up", []() {
        if (otaInProgress || ioSafeState) { server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(503, "text/plain", "Motor gesperrt"); return; }
        if (motorState == MOTOR_STOPPED) { motorReason = "Service"; startMotorOpen(openPosition); }
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
    });

    server.on("/motor/down", []() {
        if (otaInProgress || ioSafeState) { server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(503, "text/plain", "Motor gesperrt"); return; }
        if (motorState == MOTOR_STOPPED) { motorReason = "Service"; startMotorClose(closePosition); }
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
    });

    server.on("/motor/stop", []() { motorStop(); motorState = MOTOR_STOPPED; server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK"); });

    server.on("/calib-status", []() {
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "application/json", "{\"open\":" + String(openPosition) + ",\"close\":" + String(closePosition) + "}");
    });

    server.on("/learn-status", []() {
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "application/json", "{\"active\":" + String(learningActive?"true":"false") + ",\"phase\":" + String(learningOpenDone?2:1) + "}");
    });

    server.on("/systemtest-status", HTTP_GET, []() {
        updateSystemHealth();
        JsonDocument doc;
        doc["wifi"]           = (WiFi.status() == WL_CONNECTED);
        doc["rssi"]           = WiFi.RSSI();
        doc["mqtt"]           = mqttClientConnected();
        doc["lux"]            = (hasVEML && !vemlHardError) ? lux : -1;
        doc["bhOk"]           = (hasVEML && !vemlHardError);
        doc["rtcOk"]          = rtcOk ? 1 : 0;
        doc["rtcStatus"]      = rtcOk ? "OK" : "Nicht gefunden / nicht initialisiert";
        doc["bmeOk"]          = bmeOk;
        doc["bmeTemp"]        = bmeOk ? String(bmeTemp,     1) : "n/a";
        doc["bmeHumidity"]    = bmeOk ? String(bmeHumidity, 1) : "n/a";
        doc["bmePressure"]    = bmeOk ? String(bmePressure, 1) : "n/a";
        doc["heap"]           = ESP.getFreeHeap();
        doc["uptime"]         = millis() / 1000;
        doc["useLimitSwitches"] = useLimitSwitches;
        String out; serializeJson(doc, out);
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "application/json", out);
    });

    server.on("/systemtest-motor", HTTP_POST, []() {
        if (motorState == MOTOR_STOPPED && !otaInProgress && !ioSafeState) {
            startMotorOpen(200); delay(250); motorStop();
        }
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
    });

    server.on("/mqtt-test", HTTP_POST, []() {
    String host     = server.arg("host");
    int    port     = server.arg("port").toInt();
    String user     = server.arg("user");
    String pass     = server.arg("pass");
    String clientId = server.arg("clientId");
    if (port <= 0 || port > 65535) port = 1883;

    WiFiClient tc;
    // TCP-Connect mit 4s Timeout – schlägt schnell fehl statt 10-20s zu hängen
    if (!tc.connect(host.c_str(), (uint16_t)port, 4000)) {
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "FAIL");
        return;
    }

    // TCP ist offen → PubSubClient macht nur noch den MQTT-Handshake
    PubSubClient tm(tc);
    tm.setServer(host.c_str(), (uint16_t)port);
    bool ok = user.length() > 0
        ? tm.connect(clientId.c_str(), user.c_str(), pass.c_str())
        : tm.connect(clientId.c_str());

    if (ok) tm.disconnect();
    server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", ok ? "OK" : "FAIL");
});

    server.on("/reset", HTTP_POST, []() {
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "Restarting");
        delay(500); ESP.restart();
    });

    server.on("/manifest.json", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "application/json; charset=utf-8", R"JSON({
  "name": "H\u00FChnerklappe",
  "short_name": "Klappe",
  "description": "Automatische H\u00FChnerklappe Steuerung",
  "start_url": "/",
  "scope": "/",
  "display": "standalone",
  "orientation": "portrait",
  "background_color": "#0f172a",
  "theme_color": "#1e293b",
  "icons": [
    {"src": "/icon192", "sizes": "192x192", "type": "image/png", "purpose": "any maskable"},
    {"src": "/icon512", "sizes": "512x512", "type": "image/png", "purpose": "any maskable"}
  ]
})JSON");
    });

    server.on("/icon192", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "public, max-age=604800");
        server.client().setNoDelay(true); server.send_P(200, "image/png", (PGM_P)icon192_png, icon192_png_len);
    });
    server.on("/icon512", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "public, max-age=604800");
        server.client().setNoDelay(true); server.send_P(200, "image/png", (PGM_P)icon512_png, icon512_png_len);
    });
    server.on("/icon-192.png", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "public, max-age=604800");
        server.client().setNoDelay(true); server.send_P(200, "image/png", (PGM_P)icon192_png, icon192_png_len);
    });
    server.on("/icon-512.png", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "public, max-age=604800");
        server.client().setNoDelay(true); server.send_P(200, "image/png", (PGM_P)icon512_png, icon512_png_len);
    });
    // Standard-Pfade die Safari automatisch sucht
    server.on("/apple-touch-icon.png", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "public, max-age=604800");
        server.client().setNoDelay(true); server.send_P(200, "image/png", (PGM_P)icon192_png, icon192_png_len);
    });
    server.on("/apple-touch-icon-precomposed.png", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "public, max-age=604800");
        server.client().setNoDelay(true); server.send_P(200, "image/png", (PGM_P)icon192_png, icon192_png_len);
    });

    // Forecast-Test
    server.on("/test/forecast/start", HTTP_GET, []() {
        // Parameter aus Simulation-Seite übernehmen falls vorhanden
        if (server.hasArg("start")) testLuxStart    = server.arg("start").toFloat();
        if (server.hasArg("end"))   testLuxEnd      = server.arg("end").toFloat();
        if (server.hasArg("dur"))   testDurationMin = server.arg("dur").toFloat();
        forecastTestMode = true; luxReady = true; testStartMillis = millis();
        manualOverrideUntil = 0;
        lastLux = testLuxStart; lastLuxTime = millis(); luxRateFiltered = 0;
        addLog("TEST: Lux-Kurve " + String(testLuxStart,0) + "→" + String(testLuxEnd,0) + " lx über " + String(testDurationMin,0) + " min");
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "Prognose-Test gestartet");
    });
    server.on("/test/forecast/stop", HTTP_GET, []() {
        forecastTestMode = false; addLog("TEST: Prognose-Test beendet");
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "Prognose-Test beendet");
    });

    // Simulation
    server.on("/simulation",        HTTP_GET, handleSimulation);
    server.on("/simulation-status", HTTP_GET, handleSimulationStatus);
    server.on("/simulation-set",    HTTP_GET, handleSimulationSet);
    server.on("/simulation-lux",    HTTP_GET, []() {
        String action = server.arg("action");
        if (action == "set") {
            float v = server.arg("lux").toFloat();
            v = constrain(v, 0.0f, 120000.0f);
            simLuxSet(v);
            luxReady = true;  // Automatik sofort aktivieren
            addLog("💡 Sim-Lux: " + String(v, 1) + " lx");
        } else {
            simLuxClear();
            addLog("💡 Sim-Lux deaktiviert");
        }
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
    });

    // OTA
    server.on("/update", HTTP_POST,
        []() {
            bool ok = !Update.hasError() && Update.end(true);
            Serial.printf("OTA end: ok=%d error=%d\n", ok, Update.getError());
            if (!ok) Update.printError(Serial);
            server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(ok ? 200 : 500, "text/plain; charset=UTF-8", ok ? "Update erfolgreich" : "Update fehlgeschlagen");
            otaInProgress = false;
            ioSafeState   = false;
            if (ok) { delay(300); ESP.restart(); }
        },
        []() {
            HTTPUpload &u = server.upload();
            if (u.status == UPLOAD_FILE_START) {
                // Nur das Nötigste – kein MQTT, kein Log, kein Blocking!
                otaInProgress = true;
                ioSafeState   = true;
                motorStop();
                digitalWrite(MOTOR_IN1, LOW);
                digitalWrite(MOTOR_IN2, LOW);
                lightOff();
                stallLightOff();
                Serial.printf("OTA start: %s\n", u.filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
            }
            else if (u.status == UPLOAD_FILE_WRITE) {
                if (Update.write(u.buf, u.currentSize) != u.currentSize)
                    Update.printError(Serial);
            }
            else if (u.status == UPLOAD_FILE_END) {
                Serial.printf("OTA upload done: %d bytes\n", u.totalSize);
            }
            else if (u.status == UPLOAD_FILE_ABORTED) {
                Serial.println("OTA aborted");
                Update.abort();
                otaInProgress = false;
                ioSafeState   = false;
            }
        }
    );

    // ===== RELAIS-TEST (Boot-Indikator) =====
    lightOn();
    { unsigned long t = millis(); while (millis() - t < 1000) yield(); }
    lightOff();
    { unsigned long t = millis(); while (millis() - t < 200)  yield(); }

    bootTime = millis();
    server.begin();
    addLog("🚀 Hühnerklappe gestartet – FW " + String(FW_VERSION));
    addLog("🌐 Erreichbar unter http://klappe.local oder http://" + WiFi.localIP().toString());
}

// ==========================
// LOOP
// ==========================
void loop()
{
    wdogFeed();            // Software-Watchdog

    // WebServer bei jedem Durchlauf bedienen
    server.handleClient();

    // Während OTA-Upload: nur WebServer, kein delay, keine Logik
    // Während OTA-Upload: nur WebServer, kein delay, keine Logik
    if (otaInProgress) {
        statusLedOta();   // Blaues Lauflicht während OTA
        return;
    }

    const unsigned long nowMs = millis();
    if (nowMs - lastLogicRun < LOGIC_INTERVAL) return;  // kein delay – sofort zurück
    lastLogicRun = nowMs;

    // ===== NETZWERK =====
    mqttLoop();
    server.handleClient();   // Web-Requests zwischen schweren Operationen abarbeiten
    wifiWatchdog();

    // NTP Nachsync
    static bool ntpOk = false;
    static unsigned long lastNtpRetry = 0;
    if (!ntpOk && millis() - lastNtpRetry > 10000 && WiFi.status() == WL_CONNECTED) {
        lastNtpRetry = millis();
        struct tm ti;
        if (getLocalTime(&ti, 0)) {
            ntpOk = true;
            Serial.printf("✅ NTP nachsynchronisiert: %02d.%02d.%04d %02d:%02d:%02d\n",
                ti.tm_mday, ti.tm_mon+1, ti.tm_year+1900, ti.tm_hour, ti.tm_min, ti.tm_sec);
        }
    }

    // ===== MOTOR + TASTER =====
    updateMotor();
    updateButton();
    updateStallButton();
    updateRedButton();

    // ===== LUX LESEN (alle 1s) =====
    float rawLux = NAN;
    bool  luxValid = false;
    bool  luxReadAttempted = false;

    if (nowMs - lastLuxRead > 1000)
    {
        lastLuxRead      = nowMs;
        luxReadAttempted = true;

        if (forecastTestMode)
        {
            float elapsed = (nowMs - testStartMillis) / 60000.0f;
            if (elapsed > testDurationMin) elapsed = testDurationMin;
            rawLux = testLuxStart - (testLuxStart - testLuxEnd) * (elapsed / testDurationMin);
            lightAutomationAvailable = true;
        }
        else rawLux = getLux();

        if (isfinite(rawLux)) rawLux = medianLux(rawLux);
    }
    luxValid = isfinite(rawLux) && rawLux >= 0.0f;

    // ===== LUX-FEHLERÜBERWACHUNG =====
    if (luxReadAttempted)
        checkLuxHealth(nowMs, rawLux, luxValid);

    // ===== EMA FILTER =====
    if (luxValid)
    {
        vemlLastLux = nowMs;
        if (!luxInitDone) { luxFiltered = rawLux; luxInitDone = true; }
        else luxFiltered = luxFiltered * 0.8f + rawLux * 0.2f;
        lux = luxFiltered;
    }

    // ===== TREND =====
    if (!luxReady && lux > 5)
    {
        luxReady   = true;
        lastLux    = lux;
        lastLuxTime = nowMs;
        luxRateFiltered = 0;
    }
    if (luxValid && nowMs - lastTrendUpdate > 30000)
    {
        updateLuxTrend(lux);
        lastTrendUpdate = nowMs;
    }

    // ===== SYSTEM-HEALTH =====
    updateSystemHealth();

    // ===== BME280 =====
    bmeUpdate();
    relaySync();
    loggerUpdate();  // LittleFS deferred write alle 30s
    server.handleClient();   // Web-Requests nach I2C/Sensor-Arbeit abarbeiten

    // ===== DIMMING + STALLLICHT =====
    updateDimming(nowMs);
    updateStallLightTimer(nowMs);

    // ===== OHNE RTC: NTP-Zeit verwenden wenn verfügbar =====
    DateTime nowDT = nowRTC();
    if (nowDT.year() < 2020) { updateLightState(); return; }

    // ===== AUTOMATIK =====
    DateTime now    = nowDT;
    int      nowMin = now.hour() * 60 + now.minute();

    // ===== AUTO-NEUSTART bei VEML-Ausfall (03:00) =====
    static int lastRestartDay = -1;
    if (vemlHardError && now.hour() == 3 && now.minute() == 0 && now.day() != lastRestartDay) {
        lastRestartDay = now.day();
        addLog("⚠️ VEML-Ausfall: Geplanter Neustart 03:00");
        loggerUpdate();  // Logs sofort sichern
        delay(500);
        ESP.restart();
    }

    // ===== TAGES-STATISTIK um 23:55 per MQTT =====
    static int lastStatDay = -1;
    if (now.hour() == 23 && now.minute() == 55 && now.day() != lastStatDay) {
        lastStatDay = now.day();
        if (mqttClientConnected()) {
            extern int statOpenCount, statCloseCount;
            extern unsigned long statOpenDurationMs;
            JsonDocument statDoc;
            statDoc["date"]          = String(now.day()) + "." + String(now.month()) + "." + String(now.year());
            statDoc["opens"]         = statOpenCount;
            statDoc["closes"]        = statCloseCount;
            int openMinutes          = (int)(statOpenDurationMs / 60000UL);
            statDoc["open_min"]      = openMinutes;
            statDoc["sensor_errors"] = vemlHardError ? 1 : 0;
            String statOut; serializeJson(statDoc, statOut);
            extern void mqttPublishRaw(const String &topic, const String &payload);
            mqttPublishRaw("tele/daily", statOut);
            addLog("📊 Tagesstatistik: " + String(statOpenCount) + "× geöffnet, "
                   + String(statCloseCount) + "× geschlossen, "
                   + String(openMinutes) + " min offen");
        }
        // Statistik zurücksetzen
        extern int statOpenCount, statCloseCount;
        extern unsigned long statOpenDurationMs;
        statOpenCount = 0; statCloseCount = 0; statOpenDurationMs = 0;
    }

    runAutomatik(now, nowMin, nowMs, luxValid, luxReady, luxRateFiltered);

    // ===== TELEGRAM DEADLINE =====
    telegramDeadlineCheck();

    // ===== LICHT-ZUSTANDSMASCHINE =====
    updateLightState();

    // ===== STATUS-LED =====
    statusLedUpdate();
}