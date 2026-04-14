# 💬 Telegram-Benachrichtigungen

🇩🇪 Deutsch · Firmware `3.0.16`

Die Klappe kann wichtige Ereignisse als Push-Nachricht an einen Telegram-
Chat senden. Es werden **keine externen Services** wie IFTTT verwendet –
die Firmware spricht direkt mit `api.telegram.org` per HTTPS.

---

## Inhaltsverzeichnis

- [Was wird gesendet?](#-was-wird-gesendet)
- [Bot anlegen](#-bot-anlegen)
- [Chat-ID herausfinden](#-chat-id-herausfinden)
- [Konfiguration im WebUI](#-konfiguration-im-webui)
- [Deadline-Alarm](#-deadline-alarm)
- [Besonderheiten & Troubleshooting](#-besonderheiten--troubleshooting)

---

## ✉ Was wird gesendet?

Alle Nachrichten beginnen mit einem einheitlichen Header `🐔 Hühnerklappe`
gefolgt vom eigentlichen Inhalt.

| Ereignis | Wann | Steuerung |
|---|---|---|
| **✅ Klappe geöffnet** | Jede Öffnung (Automatik, Web, MQTT, Taster) | Checkbox `notifyOpen` |
| **🔒 Klappe geschlossen** | Jede Schließung, inkl. aktueller Lux-Wert | Checkbox `notifyClose` |
| **⚠️ Lichtsensor ausgefallen** | VEML meldet Hard-Error (einmal pro Session) | immer, wenn aktiviert |
| **🔄 Neustart durch Watchdog** | Beim nächsten Boot nach einem WDT-Reset | immer, wenn aktiviert |
| **⚠️ Klappe noch geschlossen** | Deadline-Alarm zur eingestellten Uhrzeit | siehe unten |

Die Meldungen enthalten immer den **Grund** (`motorReason`) und die Uhrzeit;
bei Schließungen zusätzlich die gemessene Helligkeit.

---

## 🤖 Bot anlegen

1. In Telegram den Chat mit **[@BotFather](https://t.me/BotFather)** öffnen
2. Befehl `/newbot` senden
3. Namen + Benutzer­name vergeben (endend auf `bot`)
4. BotFather antwortet mit einem **Token** der Form
   `123456789:ABCdefGhiJklMnoPqrStuVwxYz`
5. Diesen Token in die Zwischenablage kopieren – er wird gleich im
   WebUI der Klappe gebraucht.

> **Wichtig:** Der Token ist ein **Geheimnis** wie ein Passwort. Nicht in
> Screenshots auf GitHub posten, nicht im Klartext im Repo einchecken.

---

## 🆔 Chat-ID herausfinden

Telegram akzeptiert Nachrichten nur an Chats, an denen der Bot bereits
teilnimmt. Für den ersten Kontakt:

1. In Telegram den neu erstellten Bot anschreiben (irgendeine Nachricht,
   z. B. `/start`)
2. Im Browser aufrufen:
   ```
   https://api.telegram.org/bot<DEIN_TOKEN>/getUpdates
   ```
3. Im zurückgegebenen JSON das Feld `chat.id` suchen. Beispiel:
   ```json
   "chat":{"id":123456789,"first_name":"Max","type":"private"}
   ```
   → Chat-ID = `123456789`
4. Für **Gruppen** ist die Chat-ID negativ (z. B. `-100123456789`).
   Dann muss der Bot vorher der Gruppe hinzugefügt werden.

---

## ⚙ Konfiguration im WebUI

Im Browser **`/telegram`** öffnen:

| Feld | Bedeutung |
|---|---|
| `enabled` | Haupt-Schalter: nur wenn aktiviert, wird überhaupt etwas gesendet |
| `token` | Bot-Token von BotFather |
| `chatId` | Ziel-Chat, auch negativ für Gruppen |
| `deadlineH` / `deadlineM` | Uhrzeit für den Deadline-Alarm |
| `notifyOpen` | Benachrichtigung bei Öffnungen |
| `notifyClose` | Benachrichtigung bei Schließungen |

Speichern über `POST /save-telegram` (wird vom Formular automatisch
ausgelöst). Die Werte landen im EEPROM ab Adresse `EEPROM_ADDR_TELEGRAM = 600`.

**Test-Nachricht:** Button „Testnachricht senden" auf derselben Seite ruft
`POST /telegram-test` auf. Ergebnis wird als HTTP-Status zurückgegeben
(`200` = Nachricht erfolgreich bei `api.telegram.org` abgesetzt).

---

## ⏰ Deadline-Alarm

Unabhängig von Öffnungs-/Schließ-Nachrichten gibt es eine tägliche
**Deadline-Prüfung**: Wenn die Klappe zur eingestellten Uhrzeit noch
**nicht offen** ist, geht eine Warnung raus.

Typischer Anwendungsfall: „Spätestens um 07:30 Uhr müssen die Hühner raus
können – falls die Automatik aus irgendeinem Grund noch geschlossen ist,
gib mir Bescheid."

**Logik** (`telegramDeadlineCheck()` in `src/telegram.cpp`):

- Läuft im Loop, prüft aber nur **einmal pro Tag** zur passenden Minute
- Benötigt eine gültige RTC/NTP-Zeit (`rtcOk`)
- Vergleicht `now.hour() == deadlineH && now.minute() == deadlineM`
- Wenn `doorOpen == false` → Nachricht raus und im Logbuch festhalten
- Nach dem Auslösen merkt sich `tgDeadlineDay`, dass heute schon geprüft
  wurde – so gibt es nie mehr als einen Alarm pro Tag

---

## 🔍 Besonderheiten & Troubleshooting

### HTTPS ohne Zertifikats-Check

Die Firmware benutzt `WiFiClientSecure::setInsecure()` – sie akzeptiert
also **jedes** TLS-Zertifikat von `api.telegram.org`. Für den Einsatz im
eigenen LAN ist das praktikabel, entspricht aber nicht dem Stand der
Technik. Wer das ändern will, muss den Root-CA-Fingerprint pinnen.

### URL-Encoding ist nur rudimentär

Der Sender ersetzt nur `%`, Leerzeichen, Zeilenumbrüche und deutsche
Umlaute. Exotische Zeichen (asiatische Schriften, Emoji die nicht
hart­codiert sind) können bei manchen Nachrichten „verschluckt" werden.
Für die Ereignis­texte reicht das Encoding aus.

### Warum kommt meine Testnachricht nicht an?

Reihenfolge der Prüfungen:

1. `enabled == true`?
2. Token mindestens 10 Zeichen?
3. `chatId` mindestens 1 Zeichen?
4. `WiFi.status() == WL_CONNECTED`?
5. TCP-Connect zu `api.telegram.org:443` erfolgreich?
6. Antwort enthält `"ok":true`?

Jeder dieser Schritte scheitert gerne in einer typischen Situation:

- Bot-Token hat keinen Doppelpunkt → irgendwo verschluckt beim Kopieren
- Chat-ID stimmt, aber der User/Bot-Chat wurde noch nie gestartet →
  `Forbidden: bot can't initiate conversation with a user`
- Gruppe mit negativer ID, aber Bot ist **nicht** Mitglied
- WLAN zu schwach, Connect zu `api.telegram.org` bricht mit Timeout ab
  (sieht man nur im seriellen Monitor: `⚠️ Telegram: Verbindung fehlgeschlagen`)

### Watchdog-Restart-Meldung

Beim Start wird der Reset-Reason geprüft (`esp_reset_reason()`), und bei
`ESP_RST_TASK_WDT` oder `ESP_RST_WDT` wartet `telegramInit()` bis zu
10 s auf WLAN und schickt dann proaktiv „🔄 Neustart durch Watchdog!".
Bei einem sauberen Power-Cycle passiert das nicht – nur bei echten Freezes.

### Sensor-Alert nur einmal pro Session

Die Flagge `tgSensorAlertSent` verhindert, dass bei anhaltendem VEML-
Ausfall alle paar Sekunden eine neue Nachricht rausgeht. Nach dem
nächsten Reboot (spätestens automatisch um 03:00 Uhr bei Hard-Error)
ist die Flagge zurückgesetzt, und ein erneuter Fehler würde eine neue
Meldung auslösen.
