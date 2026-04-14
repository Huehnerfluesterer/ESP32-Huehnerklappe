# 🌐 Web-Interface

🇩🇪 Deutsch · Firmware `3.0.16`

---

## Inhaltsverzeichnis

- [Seitenübersicht](#-seitenübersicht)
- [Startseite `/`](#-startseite-)
- [Einstellungen `/settings`](#-einstellungen-settings)
- [RGB-Farbwahl `/rgb`](#-rgb-farbwahl-rgb)
- [Erweitert `/advanced`](#-erweitert-advanced)
- [Blockade-Erkennung `/blockade`](#-blockade-erkennung-blockade)
- [ESP-NOW `/espnow`](#-esp-now-espnow)
- [MQTT `/mqtt`](#-mqtt-mqtt)
- [Telegram `/telegram`](#-telegram-telegram)
- [Lern-Modus `/learn`](#-lern-modus-learn)
- [Systemtest `/systemtest`](#-systemtest-systemtest)
- [Simulation `/simulation`](#-simulation-simulation)
- [Logbuch `/log`](#-logbuch-log)
- [OTA `/fw`](#-ota-fw)
- [Komplette Route-Referenz](#-komplette-route-referenz)

---

## 🧭 Seitenübersicht

| URL | Beschreibung |
|---|---|
| `/` | Dashboard, Echtzeit-Status |
| `/settings` | Betriebs­einstellungen (Öffnen/Schließen-Modus) |
| `/rgb` | Lichtfarbe & Helligkeit (Live-Vorschau) |
| `/advanced` | Erweiterte Einstellungen (Theme, Endschalter, BME-Quelle, Relay-MAC …) |
| `/blockade` | Blockade-Erkennung ACS712 |
| `/espnow` | ESP-NOW Status (BME-Sender MAC, Relais-Heartbeat) |
| `/mqtt` | MQTT-Konfiguration + Testverbindung |
| `/telegram` | Telegram-Bot einrichten |
| `/calibration` | Kalibrier-Werkzeuge (Direktsteuerung Motor) |
| `/learn` | Lern-Modus (Motorpositionen einlernen) |
| `/log` | Ereignis-Logbuch |
| `/systemtest` | System-Status (RAM, Uptime, Sensoren, Fehler) |
| `/simulation` | Lux/Zeit simulieren |
| `/fw` | OTA-Firmware-Update |

---

## 🏠 Startseite `/`

Das Dashboard zeigt den aktuellen Systemstatus. Ein AJAX-Poll auf
`/status` aktualisiert alle 3 s automatisch – die Seite muss nie
neu geladen werden.

**Angezeigt:**

- Datum und Uhrzeit (DS3231 / NTP-synchronisiert)
- Klappenstatus (Offen / Geschlossen / In Bewegung, inkl. %-Anzeige)
- Aktueller Lux-Wert, Trend, Prognose
- Licht-Status (Locklicht, Stalllicht, Rotlicht)
- WLAN-Signalstärke, IP
- Firmware-Version, Uptime, Freier Heap
- Letzter Motor-Reason, nächster geplanter Automatik-Trigger

**Schnellaktionen (POST/GET auf eigene Endpoints):**

- Klappe öffnen / schließen / Toggle (`/door`, `/open`, `/close`)
- Locklicht (`/light`), Stalllicht (`/stalllight`), Rotlicht (`/rgbred`)
- Manual-Override aufheben (`/clear-override`)

Das Huhn-Logo im Header ist das eigene PWA-Icon (`/icon192`).

---

## ⚙ Einstellungen `/settings`

| Einstellung | Beschreibung |
|---|---|
| Öffnen-Modus | `light` / `time` |
| Schließen-Modus | `light` / `time` |
| Öffnungszeit | Uhrzeit für Zeitsteuerung |
| Schließzeit | Uhrzeit für Zeitsteuerung |
| Lux-Öffnungsschwelle | Helligkeit in lx für Öffnung |
| Lux-Schließschwelle | Helligkeit in lx für Schließung |
| Vorlicht Öffnung | Minuten Locklicht vor Öffnung |
| Nachlicht Öffnung | Minuten Locklicht nach Öffnung |
| Vorlicht Schließung | Minuten Locklicht vor Schließung |
| Nachlicht Schließung | Minuten Locklicht nach Schließung |
| Schließverzögerung | 0–30 min (nur `light`): Lux muss so lange unter Schwelle bleiben |

Gespeichert wird getrennt per `POST /save-open` und `POST /save-close`,
damit die Buttons unabhängig sind.

> Validierung: Wenn beide Modi auf `light` stehen, muss
> `openLightThreshold > closeLightThreshold` sein – sonst Fehlermeldung.

---

## 🎨 RGB-Farbwahl `/rgb`

- **Farbwahl Locklicht:** HTML-Colorpicker + manuelle RGB+W-Eingabe + Schnellfarben
- **Live-Vorschau:** Farbe und Helligkeit ändern sich sofort beim Schieben
  (80 ms Debounce via `POST /live-rgb`)
- **Helligkeit:** Slider 1–255, wirkt live auf aktives Locklicht
- **Rotlicht-Helligkeit:** Separater Slider (`POST /red-brightness`),
  wirkt auch sofort wenn Rotlicht bereits aktiv ist
- **Speichern:** `POST /save-rgb` schreibt Farbe+Helligkeit ins EEPROM

Die RGBW-Kanäle laufen über LEDC mit 1 kHz / 8 Bit (`RGB_FREQ`, `RGB_BITS`
in `pins.h`).

---

## 🛠 Erweitert `/advanced`

Sammelseite für selten benötigte Einstellungen:

- **Theme** (`POST /set-theme`) – hell/dunkel/system
- **Endschalter nutzen?** (`POST /set-limit-switches`)
- **BME280-Quelle** (`POST /save-bme-source`): lokal I²C oder ESP-NOW
- **Relais-Node MAC** (`POST /save-relay`): Ziel-MAC für den Relais-ESP
- **Schließverzögerung** (Teil von `/save-close`)
- **Reset** (`POST /reset`) – sauberer Neustart

---

## ⚡ Blockade-Erkennung `/blockade`

Einstellung und Live-Monitoring der ACS712-basierten Motor-Blockade-
Erkennung.

| Endpoint | Methode | Beschreibung |
|---|---|---|
| `/blockade` | GET | HTML-Seite |
| `/save-blockade` | POST | `enabled=0/1`, `threshold=0.5..10.0` |
| `/blockade-live` | GET | JSON: aktueller Strom in A, laufend |
| `/blockade-peak` | GET | JSON: Peak-Strom seit Reset |
| `/blockade-peak-reset` | POST | Peak auf 0 zurücksetzen |
| `/blockade-baseline` | GET | JSON: aktuelle Null-Baseline |

**Typische Kalibrierung:**

1. `/blockade-peak-reset` aufrufen
2. Klappe manuell einmal öffnen/schließen (ohne Last)
3. Peak ablesen → z. B. `0,4 A`
4. Blockade manuell simulieren (Huhn schieben)
5. Peak-Differenz ablesen
6. Schwelle auf den Mittelwert setzen

---

## 📡 ESP-NOW `/espnow`

Statusseite für ESP-NOW:

- Anzeige der **eigenen MAC** der Klappe (für den Sender-Sketch wichtig)
- Letzte empfangene **Sender-MAC** vom BME280-Paket
- Zeit seit letztem BME-Paket (`/bme-mac`)
- Relais-Heartbeat-Alter (`/espnow-status`)
- Button **Ziel-MAC eintragen / ändern** → speichert in EEPROM
  (`saveRelaySettings`)

Siehe auch: [`espnow.de.md`](espnow.de.md).

---

## 🔌 MQTT `/mqtt`

Siehe [`mqtt.de.md`](mqtt.de.md) für das vollständige Topic-Schema.
Auf der Seite:

- Felder: Host, Port, User, Pass, ClientID, Base-Topic, Enable-Checkbox
- **Verbindungstest** per `POST /mqtt-test` – sendet einen Versuch und
  gibt HTTP 200 zurück, wenn `mqttClient.connect()` erfolgreich ist.

---

## 💬 Telegram `/telegram`

Siehe [`telegram.de.md`](telegram.de.md) für die komplette Einrichtung.
Auf der Seite:

- Bot-Token, Chat-ID
- Deadline-Uhrzeit (Alarm, wenn Klappe bis dahin nicht offen ist)
- Einzelne Events einzeln aktivierbar (`notifyOpen`, `notifyClose`)
- **Test­nachricht** per `POST /telegram-test`

---

## 🎓 Lern-Modus `/learn`

Einlernen der Motorlaufzeiten (Zeit in ms für volles Öffnen/Schließen).

| Endpoint | Methode | Zweck |
|---|---|---|
| `/learn` | GET | HTML-Seite |
| `/learn-page` | GET | Eingebettete Teilseite |
| `/learn-start` | POST | Startet die Sequenz |
| `/learn-status` | GET | JSON-Status für Polling |

Fortschritt wird live dargestellt, Positionen automatisch im EEPROM
gespeichert.

Für Feinjustierung gibt es zusätzlich **`/calibration`** mit direkten
Motor-Schaltbefehlen:

| Endpoint | Methode | Zweck |
|---|---|---|
| `/calibration` | GET | HTML |
| `/calib-status` | GET | JSON |
| `/motor/up` | GET | Motor hoch (Einzelschritt) |
| `/motor/down` | GET | Motor runter (Einzelschritt) |
| `/motor/stop` | GET | Motor sofort stoppen |

---

## 🧪 Systemtest `/systemtest`

Zeigt und testet die Hardware:

| Endpoint | Methode | Zweck |
|---|---|---|
| `/systemtest` | GET | Übersicht (RAM, Uptime, Sensoren, Fehler) |
| `/systemtest-status` | GET | JSON für Polling |
| `/systemtest-motor` | POST | Kurzer Motor-Testlauf |
| `/mqtt-test` | POST | MQTT-Verbindungstest |

---

## 🎛 Simulation `/simulation`

Ermöglicht das Testen der Automatik-Logik ohne echte Sensordaten:

- Lux-Wert manuell überschreiben (`POST /simulation-lux`)
- Zeit-Offset simulieren (`GET /simulation-set` mit `h=` / `m=` Parametern,
  intern über `simSetOffset(h, m)`)
- Status abfragen (`GET /simulation-status`)
- Alle Automatik-Entscheidungen werden wie im Normalbetrieb ausgeführt
- Nützlich zum Testen von Schwellwerten und Zeitfenstern

**Forecast-Test** (simuliert einen Lux-Verlauf über mehrere Minuten):

| Endpoint | Methode | Zweck |
|---|---|---|
| `/test/forecast/start` | GET | Startet den internen Lux-Rampen-Test |
| `/test/forecast/stop` | GET | Stoppt ihn sofort |

---

## 📋 Logbuch `/log`

Ringpuffer der letzten Ereignisse (in RAM + LittleFS persistiert, deferred
write alle 30 s durch `loggerUpdate()`).

| Endpoint | Methode | Zweck |
|---|---|---|
| `/log` | GET | HTML-Tabelle |
| `/log/download` | GET | Kompletter Logbuch-Download (`text/plain`) |
| `/log/clear` | POST | Logbuch löschen |

---

## 📦 OTA `/fw`

1. In PlatformIO: **Build** → `.pio/build/esp32-s3-devkitc-1/firmware.bin`
2. Seite `/fw` öffnen
3. `.bin`-Datei auswählen und hochladen (HTTP POST → `/update`)
4. ESP32-S3 startet automatisch neu

Der `/update`-Handler setzt `otaInProgress = true`, versetzt I/O in den
Safe-State (Motor stopp, Relais aus, Licht aus) und bedient im Loop nur
noch `server.handleClient()`. Der Task-WDT läuft währenddessen weiter.

---

## 📖 Komplette Route-Referenz

Alle Endpoints (Firmware `3.0.16`, aus `src/main.cpp`):

### Seiten (GET)

```
/                   Dashboard
/settings           Einstellungen
/advanced           Erweitert
/espnow             ESP-NOW Status
/rgb                Lichtfarbe
/blockade           Blockade-Erkennung
/fw                 OTA-Seite
/mqtt               MQTT-Seite
/telegram           Telegram-Seite
/calibration        Motor-Kalibrierung
/learn              Lern-Modus
/learn-page         Lern-Modus (Teilseite)
/log                Logbuch
/systemtest         Systemtest
/simulation         Simulation
```

### Status-/Poll-Endpoints (GET, meist JSON)

```
/status             Dashboard-JSON
/systemtest-status  Systemtest-JSON
/calib-status       Kalibrier-JSON
/learn-status       Lern-JSON
/simulation-status  Simulation-JSON
/blockade-live      aktueller Strom
/blockade-peak      Peak-Strom
/blockade-baseline  Null-Baseline
/bme-mac            letzter BME-Sender MAC
/espnow-status      Relais-Heartbeat-Alter
/mini               Health-Ping (gibt "OK" zurück)
```

### Aktionen / Save-Endpoints (POST, wenn nicht anders angegeben)

```
/save-open          POST   Öffnen-Einstellungen
/save-close         POST   Schließen-Einstellungen
/save-rgb           POST   RGB-Farbe persistieren
/save-blockade      POST   Blockade-Schwelle
/save-bme-source    POST   BME lokal / ESP-NOW
/save-relay         POST   Relais-MAC
/save-mqtt          POST   MQTT-Settings
/save-telegram      POST   Telegram-Settings

/live-rgb           POST   Live-RGB-Vorschau (80 ms Debounce)
/red-brightness     POST   Rotlicht-Helligkeit live
/clear-override     POST   Manuellen Override aufheben

/door               GET    Tür toggeln
/open               GET    Tür öffnen
/close              GET    Tür schließen
/light              GET    Locklicht toggeln
/stalllight         GET    Stalllicht toggeln
/rgbred             GET    Rotlicht toggeln

/motor/up           GET    Motor AUF
/motor/down         GET    Motor AB
/motor/stop         GET    Motor stoppen

/set-theme          POST   Theme setzen
/set-limit-switches POST   Endschalter an/aus

/log/clear          POST   Logbuch löschen
/log/download       GET    Logbuch als Text

/learn-start        POST   Lern-Modus starten
/systemtest-motor   POST   kurzer Motor-Testlauf
/mqtt-test          POST   MQTT-Verbindungstest
/telegram-test      POST   Telegram-Testnachricht
/blockade-peak-reset POST  Peak auf 0

/simulation-set     GET    Zeit-Offset setzen (?h=1&m=30)
/simulation-lux     POST   Lux simulieren

/test/forecast/start GET   Forecast-Test starten
/test/forecast/stop  GET   Forecast-Test stoppen

/reset              POST   Sauberer Neustart
/update             POST   OTA-Firmware-Upload
```

### PWA-Assets

```
/manifest.json
/icon192   /icon-192.png
/icon512   /icon-512.png
/apple-touch-icon.png
/apple-touch-icon-precomposed.png
```
