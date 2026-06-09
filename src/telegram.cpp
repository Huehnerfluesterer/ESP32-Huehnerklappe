#include "telegram.h"
#include "storage.h"     // telegramSettings
#include "system.h"
#include "door.h"
#include "logger.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <lwip/dns.h>
#include <lwip/inet.h>

extern void wdogFeed();
extern RTC_NOINIT_ATTR uint32_t diagUptimeSeconds;  // aus system.cpp

// ==================================================
// INTERNES
// ==================================================
static int    tgDeadlineDay      = -1;   // Tag, an dem Deadline geprüft wurde
static int    tgNightAlarmDay    = -1;   // Tag, an dem Nacht-Alarm geprüft wurde
static bool   tgSensorAlertSent  = false;

// Nacht-Alarm Einstellungen
bool    nightAlarmEnabled = false;
uint8_t nightAlarmH       = 23;   // Default: 23:00
uint8_t nightAlarmM       =  0;

// ==================================================
// RAW SEND (public – auch für Test-Button)
// ==================================================
bool telegramSendRaw(const String &text)
{
    if (!telegramSettings.enabled)            return false;
    if (strlen(telegramSettings.token)  < 10) return false;
    if (strlen(telegramSettings.chatId) < 1)  return false;
    if (WiFi.status() != WL_CONNECTED)        return false;

    // Nicht senden wenn WiFi-Signal zu schwach (DNS/TLS würde ewig blockieren)
    int rssi = WiFi.RSSI();
    if (rssi < -80 && rssi != 0) return false;

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(3);   // 3s für Read/Write

    wdogFeed();

    // DNS-Auflösung mit hartem 3s Timeout über lwip
    IPAddress ip;
    {
        ip_addr_t addr;
        err_t err = dns_gethostbyname("api.telegram.org", &addr, NULL, NULL);
        if (err == ERR_OK) {
            // sofort aufgelöst (im Cache)
            ip = IPAddress(addr.u_addr.ip4.addr);
        } else if (err == ERR_INPROGRESS) {
            // läuft im Hintergrund – mit kurzen Polls warten (max 3s)
            unsigned long t0 = millis();
            while (millis() - t0 < 3000) {
                delay(50);
                wdogFeed();
                if (dns_gethostbyname("api.telegram.org", &addr, NULL, NULL) == ERR_OK) {
                    ip = IPAddress(addr.u_addr.ip4.addr);
                    break;
                }
            }
        }
        if ((uint32_t)ip == 0) {
            Serial.println("⚠️ Telegram: DNS-Timeout");
            return false;
        }
    }
    wdogFeed();

    // TCP+TLS-Connect mit aufgelöster IP (kein erneutes DNS)
    if (!client.connect(ip, 443, 5000)) {   // max 5s TCP+TLS
        Serial.println("⚠️ Telegram: TLS-Verbindung fehlgeschlagen");
        return false;
    }
    wdogFeed();

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
        wdogFeed();
        if (client.available()) {
            String line = client.readStringUntil('\n');
            if (line.indexOf("\"ok\":true") >= 0) { ok = true; break; }
        }
        yield();
    }
    client.stop();
    if (ok) Serial.println("✅ Telegram: Nachricht gesendet");
    return ok;
}

// ==================================================
// PUBLIC
// ==================================================
static bool pendingWatchdogNotify = false;

void telegramInit()
{
    if (!telegramSettings.enabled) {
        Serial.println("ℹ️ Telegram deaktiviert");
        return;
    }
    Serial.println("✅ Telegram-Benachrichtigungen aktiv");

    // WDT-Benachrichtigung NICHT im Setup senden (kann Boot-Loop verursachen).
    // Stattdessen Flag setzen → wird im Loop nach 60s gesendet.
    // ABER: Nur wenn der vorherige Start lange genug lief (>120s),
    // sonst stecken wir in einer Telegram-Boot-Loop.
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT) {
        if (diagUptimeSeconds > 120)  // Letzter Lauf > 2 min → kein Boot-Loop
            pendingWatchdogNotify = true;
        else
            Serial.println("⚠️ Telegram WDT-Benachrichtigung übersprungen (möglicher Boot-Loop)");
    }
}

void telegramCheckPending()
{
    if (!pendingWatchdogNotify) return;
    if (WiFi.status() != WL_CONNECTED) return;
    if (millis() < 60000UL) return;   // 60s warten statt 30s
    pendingWatchdogNotify = false;
    telegramWatchdogRestart();
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
    // Hinweis: Auto-Reset spätestens nach 2h durchgehendem Hard-Error (siehe lux.cpp),
    // oder als Fallback um 03:00 Uhr (siehe logic.cpp).
    telegramSend("⚠️ Lichtsensor ausgefallen!\nLichtautomatik deaktiviert.\n"
                 "Auto-Reset spätestens nach 2h oder um 03:00 Uhr.");
}

void telegramSensorRecovered()
{
    if (!telegramSettings.enabled) return;
    if (!tgSensorAlertSent) return;  // kein Alarm aktiv → nichts zu melden
    tgSensorAlertSent = false;       // Flag zurücksetzen für nächste Runde
    telegramSend("✅ Lichtsensor wieder OK\nLichtautomatik reaktiviert.");
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

void telegramNightCheck()
{
    if (!telegramSettings.enabled) return;
    if (!nightAlarmEnabled) return;
    if (!rtcOk) return;
    DateTime now = nowRTC();

    if (now.hour()   == nightAlarmH &&
        now.minute() == nightAlarmM &&
        now.day()    != tgNightAlarmDay)
    {
        tgNightAlarmDay = now.day();
        if (doorOpen) {
            char buf[6]; snprintf(buf, sizeof(buf), "%02d:%02d",
                nightAlarmH, nightAlarmM);
            telegramSend("🚨 Klappe noch offen!\nEs ist " + String(buf) +
                         " Uhr und die Klappe ist noch nicht geschlossen.\nBitte prüfen – Gefahr durch Raubtiere!");
            addLog("📱 Telegram: Nacht-Alarm – Klappe um " + String(buf) + " noch offen");
        }
    }
}