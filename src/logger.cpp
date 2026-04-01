#include "logger.h"
#include "system.h"
#include <RTClib.h>
#include <LittleFS.h>

// ==================================================
// GLOBALE VARIABLEN
// ==================================================
String logbook[LOG_SIZE];
int    logIndex = 0;
static bool          fsDirty       = false;  // Logs noch nicht persistiert
static unsigned long lastFsWrite   = 0;
#define FS_WRITE_INTERVAL_MS  30000UL  // alle 30s schreiben

void mqttPublishLog(const String &line);
extern bool mqttClientConnected();

// ==================================================
// LITTLEFS INIT
// ==================================================
void loggerInit()
{
    if (!LittleFS.begin(true)) {  // true = formatieren wenn nötig
        Serial.println("⚠️ LittleFS Mount fehlgeschlagen");
        return;
    }
    Serial.println("✅ LittleFS gemountet");
    loggerLoadFromFS();
}

// ==================================================
// LOGS AUS LITTLEFS LADEN (beim Start)
// ==================================================
void loggerLoadFromFS()
{
    if (!LittleFS.exists(LOG_FILE)) {
        Serial.println("📜 Kein Logfile auf LittleFS – starte neu");
        return;
    }

    File f = LittleFS.open(LOG_FILE, "r");
    if (!f) return;

    // Alle Zeilen einlesen
    String lines[LOG_MAX_LINES];
    int count = 0;
    while (f.available() && count < LOG_MAX_LINES) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() > 0)
            lines[count++] = line;
    }
    f.close();

    // In RAM-Puffer laden
    logIndex = 0;
    for (int i = 0; i < count && i < LOG_SIZE; i++) {
        logbook[i] = lines[i];
        logIndex = (i + 1) % LOG_SIZE;
    }
    Serial.printf("📜 %d Log-Einträge aus LittleFS geladen\n", count);
}

// ==================================================
// LOG IN LITTLEFS SCHREIBEN (rotierend, max 100 Zeilen)

// ==================================================
// EINTRAG HINZUFÜGEN
// ==================================================
void addLog(const String &text)
{
    String ts;
    DateTime now = nowRTC();
    bool timeValid = rtcOk || (now.year() > 2000);
    if (timeValid) {
        char buf[20];
        snprintf(buf, sizeof(buf), "%02d.%02d.%04d %02d:%02d",
                 now.day(), now.month(), now.year(),
                 now.hour(), now.minute());
        ts = String(buf);
    } else {
        unsigned long s = millis() / 1000;
        ts = String(s / 3600) + "h" + String((s % 3600) / 60) + "m";
    }

    char entry[160];
    snprintf(entry, sizeof(entry), "%s – %s", ts.c_str(), text.c_str());

    logbook[logIndex] = String(entry);
    logIndex = (logIndex + 1) % LOG_SIZE;

    serialLog("📜 LOG: " + String(entry));

    // Dirty-Flag setzen – wird alle 30s persistiert (nicht bei jedem Eintrag)
    fsDirty = true;

    if (mqttClientConnected())
        mqttPublishLog(String(entry));
}

void addLogWithLux(const String &text, float lx)
{
    String suffix = (isfinite(lx) && lx >= 0.0f && lx <= 120000.0f)
                  ? " – " + String(lx, 1) + " lx"
                  : " – n/a lx";
    addLog(text + suffix);
}

// ==================================================
// LÖSCHEN (RAM + LittleFS)
// ==================================================
void clearLogbook()
{
    for (int i = 0; i < LOG_SIZE; i++) logbook[i] = "";
    logIndex = 0;
    // LittleFS Datei löschen
    if (LittleFS.exists(LOG_FILE))
        LittleFS.remove(LOG_FILE);
    Serial.println("📜 Logbuch gelöscht (RAM + LittleFS)");
}

// ==================================================
void serialLog(const String &msg)
{
    Serial.println(msg);
}

// ==================================================
// DEFERRED FS WRITE – im loop() aufrufen
// ==================================================
void loggerUpdate()
{
    if (!fsDirty) return;
    unsigned long now = millis();
    if (now - lastFsWrite < FS_WRITE_INTERVAL_MS) return;
    lastFsWrite = now;
    fsDirty = false;
    // Alle RAM-Einträge gesammelt schreiben
    File f = LittleFS.open(LOG_FILE, "w");
    if (!f) return;
    for (int i = 0; i < LOG_SIZE; i++) {
        int idx = (logIndex + i) % LOG_SIZE;
        if (logbook[idx].length() > 0)
            f.println(logbook[idx]);
    }
    f.close();
}

// ==================================================
String buildLogHTML()
{
    String out;
    for (int i = 0; i < LOG_SIZE; i++) {
        int idx = (logIndex + i) % LOG_SIZE;
        if (logbook[idx].length() == 0) continue;

        String css = "log-entry";
        if (logbook[idx].indexOf("ERROR") >= 0 || logbook[idx].indexOf("❌") >= 0)
            css += " error";
        else if (logbook[idx].indexOf("WARN") >= 0 || logbook[idx].indexOf("⚠️") >= 0)
            css += " warn";

        out += "<div class='" + css + "'>" + logbook[idx] + "</div>";
    }
    if (out.length() == 0)
        out = "<div class='log-entry'>Keine Einträge vorhanden</div>";
    return out;
}
