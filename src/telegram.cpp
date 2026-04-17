#include "telegram.h"
#include "storage.h"     // telegramSettings
#include "system.h"
#include "door.h"
#include "logger.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>

// ==================================================
// INTERNES
// ==================================================
static int    tgDeadlineDay      = -1;   // Tag, an dem Deadline geprüft wurde
static bool   tgSensorAlertSent  = false;

// ==================================================
// RAW SEND (public – auch für Test-Button)
// ==================================================
bool telegramSendRaw(const String &text)
{
    if (!telegramSettings.enabled)            return false;
    if (strlen(telegramSettings.token)  < 10) return false;
    if (strlen(telegramSettings.chatId) < 1)  return false;
    if (WiFi.status() != WL_CONNECTED)        return false;

    WiFiClientSecure client;
    client.setInsecure();  // Kein Zertifikat-Check – für lokale Nutzung ausreichend
    client.setTimeout(2);  // 2s statt 5s – blockiert sonst den WebServer

    if (!client.connect("api.telegram.org", 443)) {
        Serial.println("⚠️ Telegram: Verbindung fehlgeschlagen");
        return false;
    }

    // URL-Encoding (nur kritische Zeichen)
    String encoded = text;
    encoded.replace("%", "%25");
    encoded.replace(" ", "%20");
    encoded.replace("\n", "%0A");
    encoded.replace("ä", "%C3%A4"); encoded.replace("ö", "%C3%B6");
    encoded.replace("ü", "%C3%BC"); encoded.replace("Ä", "%C3%84");
    encoded.replace("Ö", "%C3%96"); encoded.replace("Ü", "%C3%9C");
    encoded.replace("ß", "%C3%9F");

    String url = "/bot" + String(telegramSettings.token) +
                 "/sendMessage?chat_id=" + String(telegramSettings.chatId) +
                 "&text=" + encoded;

    client.print("GET " + url + " HTTP/1.1\r\n"
                 "Host: api.telegram.org\r\n"
                 "Connection: close\r\n\r\n");

    bool ok = false;
    unsigned long t = millis();
    while ((client.connected() || client.available()) && millis() - t < 2000) {
    if (client.available()) {
        String line = client.readStringUntil('\n');
        if (line.indexOf("\"ok\":true") >= 0) { ok = true; break; }
    }
}
    client.stop();
    if (ok) Serial.println("✅ Telegram: Nachricht gesendet");
    return ok;
}

// ==================================================
// PUBLIC
// ==================================================
void telegramInit()
{
    if (!telegramSettings.enabled) {
        Serial.println("ℹ️ Telegram deaktiviert");
        return;
    }
    Serial.println("✅ Telegram-Benachrichtigungen aktiv");

    // Neustart-Grund prüfen
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT) {
        // Kurz warten bis WiFi verbunden
        unsigned long t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) delay(200);
        telegramWatchdogRestart();
    }
}

void telegramSend(const String &msg)
{
    telegramSendRaw("🐔 Hühnerklappe\n" + msg);
}

void telegramDoorOpened(const String &reason)
{
    if (!telegramSettings.enabled || !telegramSettings.notifyOpen) return;
    DateTime now = nowRTC();
    char buf[6]; snprintf(buf, sizeof(buf), "%02d:%02d", now.hour(), now.minute());
    telegramSend("✅ Klappe geöffnet\nGrund: " + reason + "\nUhrzeit: " + String(buf));
}

void telegramDoorClosed(const String &reason, float lux)
{
    if (!telegramSettings.enabled || !telegramSettings.notifyClose) return;
    DateTime now = nowRTC();
    char buf[6]; snprintf(buf, sizeof(buf), "%02d:%02d", now.hour(), now.minute());
    String msg = "🔒 Klappe geschlossen\nGrund: " + reason + "\nUhrzeit: " + String(buf);
    if (isfinite(lux)) msg += "\nHelligkeit: " + String(lux, 1) + " lx";
    telegramSend(msg);
}

void telegramSensorError()
{
    if (!telegramSettings.enabled) return;
    if (tgSensorAlertSent) return;  // nur einmal pro Session senden
    tgSensorAlertSent = true;
    telegramSend("⚠️ Lichtsensor ausgefallen!\nLichtautomatik deaktiviert.\nNeustart geplant um 03:00 Uhr.");
}

void telegramWatchdogRestart()
{
    if (!telegramSettings.enabled) return;
    telegramSend("🔄 Neustart durch Watchdog!\nDie Steuerung hat sich aufgehängt und wurde automatisch neu gestartet.");
}

void telegramDeadlineCheck()
{
    if (!telegramSettings.enabled) return;
    if (!rtcOk) return;
    DateTime now = nowRTC();

    // Nur einmal pro Tag zur eingestellten Uhrzeit prüfen
    if (now.hour()   == telegramSettings.deadlineH &&
        now.minute() == telegramSettings.deadlineM &&
        now.day()    != tgDeadlineDay)
    {
        tgDeadlineDay = now.day();
        if (!doorOpen) {
            char buf[6]; snprintf(buf, sizeof(buf), "%02d:%02d",
                telegramSettings.deadlineH, telegramSettings.deadlineM);
            telegramSend("⚠️ Klappe noch geschlossen!\nEs ist " + String(buf) +
                         " Uhr und die Klappe ist noch nicht geöffnet.");
            addLog("📱 Telegram: Klappe nicht geöffnet bis " + String(buf));
        }
    }
}
