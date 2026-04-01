#include "telegram.h"
#include "config.h"
#include "system.h"
#include "door.h"
#include "logger.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>

#if TELEGRAM_ENABLED

// ==================================================
// INTERNES
// ==================================================
static bool   tgReady            = false;
static int    tgDeadlineDay      = -1;   // Tag an dem Deadline geprüft wurde
static bool   tgSensorAlertSent  = false;

static void tgSendRaw(const String &text)
{
    if (!tgReady) return;
    if (WiFi.status() != WL_CONNECTED) return;

    WiFiClientSecure client;
    client.setInsecure();  // Kein Zertifikat-Check – für lokale Nutzung ausreichend
    client.setTimeout(5);

    if (!client.connect("api.telegram.org", 443)) {
        Serial.println("⚠️ Telegram: Verbindung fehlgeschlagen");
        return;
    }

    // URL-Encoding der Nachricht (einfach, nur kritische Zeichen)
    String encoded = text;
    encoded.replace("%", "%25");
    encoded.replace(" ", "%20");
    encoded.replace("\n", "%0A");
    encoded.replace("ä", "%C3%A4"); encoded.replace("ö", "%C3%B6");
    encoded.replace("ü", "%C3%BC"); encoded.replace("Ä", "%C3%84");
    encoded.replace("Ö", "%C3%96"); encoded.replace("Ü", "%C3%9C");
    encoded.replace("ß", "%C3%9F");

    String url = "/bot" + String(TELEGRAM_TOKEN) +
                 "/sendMessage?chat_id=" + String(TELEGRAM_CHAT_ID) +
                 "&text=" + encoded;

    client.print("GET " + url + " HTTP/1.1\r\n"
                 "Host: api.telegram.org\r\n"
                 "Connection: close\r\n\r\n");

    // Antwort kurz abwarten
    unsigned long t = millis();
    while (client.connected() && millis() - t < 3000) {
        if (client.available()) {
            String line = client.readStringUntil('\n');
            if (line.indexOf("\"ok\":true") >= 0) {
                Serial.println("✅ Telegram: Nachricht gesendet");
                break;
            }
        }
    }
    client.stop();
}

// ==================================================
// PUBLIC
// ==================================================
void telegramInit()
{
    tgReady = true;
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
    tgSendRaw("🐔 Hühnerklappe\n" + msg);
}

void telegramDoorOpened(const String &reason)
{
    DateTime now = nowRTC();
    char buf[6]; snprintf(buf, sizeof(buf), "%02d:%02d", now.hour(), now.minute());
    telegramSend("✅ Klappe geöffnet\nGrund: " + reason + "\nUhrzeit: " + String(buf));
}

void telegramDoorClosed(const String &reason, float lux)
{
    DateTime now = nowRTC();
    char buf[6]; snprintf(buf, sizeof(buf), "%02d:%02d", now.hour(), now.minute());
    String msg = "🔒 Klappe geschlossen\nGrund: " + reason + "\nUhrzeit: " + String(buf);
    if (isfinite(lux)) msg += "\nHelligkeit: " + String(lux, 1) + " lx";
    telegramSend(msg);
}

void telegramSensorError()
{
    if (tgSensorAlertSent) return;  // nur einmal pro Session senden
    tgSensorAlertSent = true;
    telegramSend("⚠️ Lichtsensor ausgefallen!\nLichtautomatik deaktiviert.\nNeustart geplant um 03:00 Uhr.");
}

void telegramWatchdogRestart()
{
    telegramSend("🔄 Neustart durch Watchdog!\nDie Steuerung hat sich aufgehängt und wurde automatisch neu gestartet.");
}

void telegramDeadlineCheck()
{
    if (!rtcOk) return;
    DateTime now = nowRTC();

    // Nur einmal pro Tag zur eingestellten Uhrzeit prüfen
    if (now.hour() == TELEGRAM_OPEN_DEADLINE_H &&
        now.minute() == TELEGRAM_OPEN_DEADLINE_M &&
        now.day() != tgDeadlineDay)
    {
        tgDeadlineDay = now.day();
        if (!doorOpen) {
            char buf[6]; snprintf(buf, sizeof(buf), "%02d:%02d",
                TELEGRAM_OPEN_DEADLINE_H, TELEGRAM_OPEN_DEADLINE_M);
            telegramSend("⚠️ Klappe noch geschlossen!\nEs ist " + String(buf) +
                         " Uhr und die Klappe ist noch nicht geöffnet.");
            addLog("📱 Telegram: Klappe nicht geöffnet bis " + String(buf));
        }
    }
}

#else
// Stubs wenn Telegram deaktiviert
void telegramInit()           {}
void telegramSend(const String&) {}
void telegramDoorOpened(const String&) {}
void telegramDoorClosed(const String&, float) {}
void telegramSensorError()    {}
void telegramWatchdogRestart(){}
void telegramDeadlineCheck()  {}
#endif
