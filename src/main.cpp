// ==========================
// FIRMWARE VERSION
// ==========================
const char *FW_VERSION = "4.0.1";

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
#include "config.h"
#if __has_include("rom/rtc.h")
#include "rom/rtc.h"             // rtc_get_reset_reason() per Core
#elif __has_include("esp32s3/rom/rtc.h")
#include "esp32s3/rom/rtc.h"
#endif

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
    Serial.setTxTimeoutMs(0);  // USB CDC: Nie blockieren wenn kein Host verbunden!
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
    loadNightAlarm();
    loadDoor2Settings();
    loadDoor2State();
    loadDoor2MotorPositions();
    loadDoor2LimitSwitchSetting();
    loadDoor2CloseDelay();
    loadDoor2BlockadeThreshold();

    // ===== GPIO =====
    pinMode(MOTOR_IN1,          OUTPUT); digitalWrite(MOTOR_IN1, LOW);
    pinMode(MOTOR_IN2,          OUTPUT); digitalWrite(MOTOR_IN2, LOW);
    pinMode(MOTOR2_IN1,         OUTPUT); digitalWrite(MOTOR2_IN1, LOW);
    pinMode(MOTOR2_IN2,         OUTPUT); digitalWrite(MOTOR2_IN2, LOW);
    pinMode(RELAIS_PIN,         OUTPUT); digitalWrite(RELAIS_PIN, RELAY_OFF);
    pinMode(STALLLIGHT_RELAY_PIN,OUTPUT);digitalWrite(STALLLIGHT_RELAY_PIN, STALLLIGHT_OFF);
    pinMode(BUTTON_PIN,         INPUT_PULLUP);
    pinMode(STALL_BUTTON_PIN,   INPUT_PULLUP);
    pinMode(RED_BUTTON_PIN,     INPUT_PULLUP);
    pinMode(LIMIT_OPEN_PIN,     INPUT_PULLUP);
    pinMode(LIMIT_CLOSE_PIN,    INPUT_PULLUP);
    pinMode(LIMIT2_OPEN_PIN,    INPUT_PULLUP);
    pinMode(LIMIT2_CLOSE_PIN,   INPUT_PULLUP);

    // ===== ENDSCHALTER-VERIFIKATION NACH BOOT =====
    // Korrigiert doorOpen wenn der gespeicherte Zustand nicht zur
    // tatsächlichen Klappenposition passt (z.B. nach Restart während Schließvorgang)
    if (useLimitSwitches)
    {
        delay(10);  // Pullups stabilisieren lassen
        bool limitOpenActive  = (digitalRead(LIMIT_OPEN_PIN)  == LOW);
        bool limitCloseActive = (digitalRead(LIMIT_CLOSE_PIN) == LOW);

        if (limitOpenActive && !doorOpen)
        {
            doorOpen  = true;
            doorPhase = PHASE_OPEN;
            saveDoorState();
            addLog("⚠️ Boot-Korrektur: Endschalter OBEN aktiv → Klappe als offen markiert");
        }
        else if (limitCloseActive && doorOpen)
        {
            doorOpen  = false;
            doorPhase = PHASE_IDLE;
            saveDoorState();
            addLog("⚠️ Boot-Korrektur: Endschalter UNTEN aktiv → Klappe als geschlossen markiert");
        }
        // Hinweis: Wenn keiner der Endschalter aktiv ist, vertrauen wir dem
        // gespeicherten EEPROM-Wert. doorOpen wird erst NACH erfolgreichem
        // Schließvorgang auf false gesetzt – ein Reset während des Schließens
        // hinterlässt also doorOpen=true, was korrekt ist (Tür ist noch offen).
    }

    // ===== RGB + MOTOR =====
    lightInit();
    statusLedInit();   // Status-LED (WS2812 IO48) initialisieren
    motorInit();
    motor2Init();
    digitalWrite(MOTOR_IN1, LOW);
    digitalWrite(MOTOR_IN2, LOW);
    ledcWrite(3, 0);
    digitalWrite(MOTOR2_IN1, LOW);
    digitalWrite(MOTOR2_IN2, LOW);
    ledcWrite(4, 0);

    // ===== I2C + RTC + VEML =====
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);   // 100 kHz – stabiler als 30 kHz
    Wire.setTimeOut(20);     // 20ms Timeout (war 50ms)
    rtcOk = rtc.begin();
    if (!rtcOk) Serial.println("⚠️ RTC DS3231 nicht gefunden");
    luxInit();
    luxTaskStart();   // Lux-Lesen läuft jetzt auf Core 0 → blockierender I2C kann den Loop nicht mehr lahmlegen
    // bmeInit() nach WiFi – ESP-NOW benötigt initialisierten WiFi-Stack

    // ===== WIFI + NTP =====
    // Hostname wird in wifiConnectNonBlocking() gesetzt (korrekte Reihenfolge!)
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

    // Klappe 2
    if (door2Open) { door2Phase = PHASE_OPEN; }
    else           { door2Phase = PHASE_IDLE; }

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

    // Klappe 2 Einstellungen
    server.on("/door2-settings", HTTP_GET, handleDoor2Settings);
    server.on("/save-door2-open",  HTTP_POST, handleSaveDoor2Open);
    server.on("/save-door2-close", HTTP_POST, handleSaveDoor2Close);
    server.on("/set-door2-limit-switches", HTTP_POST, []() {
        if (server.hasArg("enabled")) {
            door2UseLimitSwitches = server.arg("enabled") == "1";
            EEPROM.put(EEPROM_ADDR_DOOR2_LIMIT_SW, door2UseLimitSwitches); EEPROM.commit();
            addLog(String("Klappe2 Endschalter ") + (door2UseLimitSwitches ? "aktiviert" : "deaktiviert"));
        }
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
    });

    // Klappe 2 Kalibrierung – Stoppuhr
    server.on("/save-door2-calib", HTTP_POST, []() {
        String dir = server.arg("dir");
        long ms = server.arg("ms").toInt();
        if (ms < 500 || ms > 60000) ms = 6000;
        if (dir == "open") {
            door2OpenPosition = ms;
        } else {
            door2ClosePosition = ms;
        }
        saveDoor2MotorPositions();
        addLog("Klappe2 " + dir + " kalibriert: " + String(ms) + " ms");
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
    });

    // Klappe 2 Kalibrierung – Manuell
    server.on("/save-door2-calib-manual", HTTP_POST, []() {
        long op = server.arg("open").toInt();
        long cp = server.arg("close").toInt();
        if (op < 500 || op > 60000) op = 6000;
        if (cp < 500 || cp > 60000) cp = 6000;
        door2OpenPosition  = op;
        door2ClosePosition = cp;
        saveDoor2MotorPositions();
        addLog("Klappe2 manuell kalibriert: open=" + String(op) + " close=" + String(cp) + " ms");
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
    });

    // Klappe 2 Blockade-Schwelle
    server.on("/save-door2-blockade", HTTP_POST, []() {
        door2BlockadeThresholdA = server.arg("threshold").toFloat();
        if (door2BlockadeThresholdA < 0.5f || door2BlockadeThresholdA > 10.0f) door2BlockadeThresholdA = 2.0f;
        saveDoor2BlockadeThreshold();
        addLog("Klappe2 Blockade-Schwelle: " + String(door2BlockadeThresholdA, 1) + " A");
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
    });
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
        if (motor2State != MOTOR_STOPPED) {
            server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(409, "text/plain", "Klappe 2 Motor läuft"); return;
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

    // ===== KLAPPE 2 TOGGLE =====
    server.on("/door2", []() {
        if (otaInProgress || ioSafeState) { server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(503, "text/plain", "OTA aktiv"); return; }
        if (motor2State != MOTOR_STOPPED) {
            motor2Stop(); motor2State = MOTOR_STOPPED; motor2Reason = "Stop/Manuell";
            door2Phase = door2Open ? PHASE_OPEN : PHASE_IDLE;
            addLog("Klappe2 Motor gestoppt"); server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "STOP"); return;
        }
        if (isAnyMotorRunning()) {
            server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(409, "text/plain", "Anderer Motor läuft"); return;
        }
        if (door2Open) {
            door2Phase = PHASE_CLOSING; motor2Reason = "manuell/Web";
            startMotor2Close(door2ClosePosition); actionLock2 = true;
            server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "Closing2"); addLog("Klappe2 Schließvorgang (Web)");
        } else {
            door2Phase = PHASE_OPENING; motor2Reason = "manuell/Web";
            startMotor2Open(door2OpenPosition); actionLock2 = true;
            server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "Opening2"); addLog("Klappe2 Öffnung (Web)");
        }
    });

    // Klappe 2 Motor-Service-Endpunkte
    server.on("/motor2/up", []() {
        if (otaInProgress || ioSafeState || isAnyMotorRunning()) { server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(503, "text/plain", "Gesperrt"); return; }
        motor2Reason = "Service"; startMotor2Open(30000);  // 30s Timeout für Kalibrierung
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
    });
    server.on("/motor2/down", []() {
        if (otaInProgress || ioSafeState || isAnyMotorRunning()) { server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(503, "text/plain", "Gesperrt"); return; }
        motor2Reason = "Service"; startMotor2Close(30000);  // 30s Timeout für Kalibrierung
        server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK");
    });
    server.on("/motor2/stop", []() { motor2Stop(); motor2State = MOTOR_STOPPED; server.client().setNoDelay(true); server.sendHeader("Connection","close"); server.send(200, "text/plain", "OK"); });

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
        lastRestartSource = RESTART_WEB_UI;
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
            if (ok) { lastRestartSource = RESTART_OTA; delay(300); ESP.restart(); }
        },
        []() {
            HTTPUpload &u = server.upload();
            if (u.status == UPLOAD_FILE_START) {
                // Nur das Nötigste – kein MQTT, kein Log, kein Blocking!
                otaInProgress = true;
                ioSafeState   = true;
                motorStop();
                motor2Stop();
                digitalWrite(MOTOR_IN1, LOW);
                digitalWrite(MOTOR_IN2, LOW);
                digitalWrite(MOTOR2_IN1, LOW);
                digitalWrite(MOTOR2_IN2, LOW);
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

    // ===== RELAIS-TEST (Boot-Indikator) – deaktiviert =====
    // lightOn();
    // { unsigned long t = millis(); while (millis() - t < 1000) yield(); }
    // lightOff();
    // { unsigned long t = millis(); while (millis() - t < 200)  yield(); }

    bootTime = millis();
    server.begin();
    // ===== FIRMWARE-UPDATE-ERKENNUNG =====
    // Wenn die im EEPROM gespeicherte FW-Version von der aktuellen abweicht,
    // war der letzte Reset ein Firmware-Update (OTA oder USB-Flash).
    char savedFwVersion[16] = {0};
    EEPROM.get(EEPROM_ADDR_LAST_FW, savedFwVersion);
    savedFwVersion[15] = '\0';  // Sicherheit
    bool firmwareUpdated = (strncmp(savedFwVersion, FW_VERSION, 15) != 0);
    if (firmwareUpdated) {
        // Aktuelle Version speichern für nächsten Boot
        char currentFw[16] = {0};
        strncpy(currentFw, FW_VERSION, 15);
        EEPROM.put(EEPROM_ADDR_LAST_FW, currentFw);
        EEPROM.commit();
    }

    addLog("🚀 Hühnerklappe gestartet – FW " + String(FW_VERSION));
    addLog("🌐 Erreichbar unter http://" + String(DEVICE_HOSTNAME) + ".local oder http://" + WiFi.localIP().toString());

    // Neustart-Grund loggen
    esp_reset_reason_t reason = esp_reset_reason();
    String reasonStr;
    switch (reason) {
        case ESP_RST_POWERON:  reasonStr = "Eingeschaltet";      break;
        case ESP_RST_SW:       reasonStr = firmwareUpdated ? "Firmware-Update" : "Software-Restart"; break;
        case ESP_RST_PANIC:    reasonStr = "Kernel-Panic/Crash"; break;
        case ESP_RST_INT_WDT:  reasonStr = "Interrupt-Watchdog"; break;
        case ESP_RST_TASK_WDT: reasonStr = "Task-Watchdog";      break;
        case ESP_RST_WDT:      reasonStr = "Watchdog";           break;
        case ESP_RST_BROWNOUT: reasonStr = "Unterspannung";      break;
        default:               reasonStr = "Unbekannt (" + String((int)reason) + ")"; break;
    }
    addLog("🔄 Neustart-Grund: " + reasonStr);
    addLog("💾 Freier Heap: " + String(ESP.getFreeHeap() / 1024) + " KB");

    // Per-Core Reset-Reason (detaillierter als esp_reset_reason)
    // 1=PowerOn, 3=SW_RESET, 12=SW_CPU_RESET, 14=TG0WDT, 15=TG1WDT, 16=RTCWDT
    addLog("🔬 Reset-Code Core0: " + String(rtc_get_reset_reason(0))
         + " Core1: " + String(rtc_get_reset_reason(1)));

    // Bei Firmware-Update: keine Crash-Diagnose anzeigen (würde nur verwirren)
    if (firmwareUpdated) {
        if (strlen(savedFwVersion) > 0)
            addLog("⬆️ Firmware aktualisiert: " + String(savedFwVersion) + " → " + String(FW_VERSION));
        else
            addLog("⬆️ Firmware: Erstinstallation oder EEPROM neu");
    }
    else if (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
             reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT ||
             reason == ESP_RST_SW) {
        addLog("📍 Letzte Position vor Restart: " + crumbName(crashBreadcrumb));
        addLog("🔎 Restart-Quelle: " + restartSourceName(lastRestartSource));
    }
    crashBreadcrumb   = CRUMB_IDLE;
    lastRestartSource = RESTART_UNKNOWN;
    if (!firmwareUpdated) {
        diagLogBootInfo();   // Heap/Stack/Uptime-Diagnose vom vorherigen Lauf
    }
    diagReset();             // Diagnosewerte für neuen Lauf zurücksetzen
}

// ==========================
// LOOP
// ==========================
void loop()
{
    wdogFeed();

    setCrumb(CRUMB_WEBSERVER);
    server.handleClient();
    yield();   // WiFi-Stack CPU-Zeit geben

    if (otaInProgress) {
        wdogFeed();
        statusLedOta();
        return;
    }

    const unsigned long nowMs = millis();
    if (nowMs - lastLogicRun < LOGIC_INTERVAL) return;
    lastLogicRun = nowMs;
    wdogFeed();

    // ===== NETZWERK =====
    setCrumb(CRUMB_MQTT);
    mqttLoop();
    wdogFeed();

    setCrumb(CRUMB_WEBSERVER);
    // server.handleClient() bereits am Loop-Anfang

    setCrumb(CRUMB_WIFI);
    wifiWatchdog();
    wdogFeed();

    // NTP Nachsync
    setCrumb(CRUMB_NTP);
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
    setCrumb(CRUMB_MOTOR);
    updateMotor();
    updateMotor2();
    updateButton();
    updateStallButton();
    updateRedButton();
    wdogFeed();

    // ===== LUX LESEN (alle 1s) =====
    setCrumb(CRUMB_LUX);
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
        else {
            // Lux-Wert vom Hintergrund-Task abholen (nicht-blockierend)
            // Wenn der Task hängt, kommt NAN zurück und das System läuft weiter
            rawLux = luxTaskGetValue();
        }

        if (isfinite(rawLux)) rawLux = medianLux(rawLux);
    }
    luxValid = isfinite(rawLux) && rawLux >= 0.0f;

    // ===== LUX-FEHLERÜBERWACHUNG =====
    setCrumb(CRUMB_I2C_HEALTH);
    if (luxReadAttempted)
        checkLuxHealth(nowMs, rawLux, luxValid);
    wdogFeed();

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
    setCrumb(CRUMB_BME);
    bmeUpdate();
    relaySync();
    wdogFeed();

    setCrumb(CRUMB_LITTLEFS);
    loggerUpdate();

    // ===== DIMMING + STALLLICHT =====
    setCrumb(CRUMB_LIGHT);
    updateDimming(nowMs);
    updateStallLightTimer(nowMs);

    // ===== OHNE RTC: NTP-Zeit verwenden wenn verfügbar =====
    DateTime nowDT = nowRTC();
    if (nowDT.year() < 2020) { updateLightState(); return; }

    // ===== AUTOMATIK =====
    DateTime now    = nowDT;
    int      nowMin = now.hour() * 60 + now.minute();

    // VEML-Neustart wird in logic.cpp/runAutomatik() um 03:00 behandelt
    // (inkl. Statistik-Sicherung vor dem Restart)

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

    setCrumb(CRUMB_AUTOMATIK);
    runAutomatik(now, nowMin, nowMs, luxValid, luxReady, luxRateFiltered);
    runAutomatik2(now, nowMin, nowMs, luxValid, luxReady, luxRateFiltered);
    wdogFeed();

    // ===== TELEGRAM =====
    setCrumb(CRUMB_TELEGRAM);
    telegramCheckPending();
    telegramDeadlineCheck();
    telegramNightCheck();
    wdogFeed();

    // ===== LICHT-ZUSTANDSMASCHINE =====
    setCrumb(CRUMB_LIGHT);
    updateLightState();

    // ===== STATUS-LED =====
    statusLedUpdate();

    // ===== CRASH-DIAGNOSTIK (alle 10s) =====
    {
        static unsigned long lastDiagRun = 0;
        if (nowMs - lastDiagRun > 10000UL) {
            lastDiagRun = nowMs;
            diagUpdate();  // Heap/Stack/Uptime in RTC-Memory sichern

            uint32_t freeHeap = ESP.getFreeHeap();
            if (freeHeap < 30000) {
                addLog("⚠️ Heap kritisch: " + String(freeHeap/1024) + " KB frei, Min: " + String(ESP.getMinFreeHeap()/1024) + " KB");
            }
            if (freeHeap < 20000) {
                addLog("🚨 Heap < 20 KB – präventiver Neustart");
                loggerUpdate();
                lastRestartSource = RESTART_HEAP_LOW;
                delay(100);
                ESP.restart();
            }
        }
    }

    setCrumb(CRUMB_IDLE);
    yield();   // WiFi-Stack und andere Tasks bedienen
}