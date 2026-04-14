# 📦 Installationsanleitung

🇩🇪 Deutsch

---

## Inhaltsverzeichnis

- [Voraussetzungen](#-voraussetzungen)
- [Schritt 1 – Repository klonen](#schritt-1--repository-klonen)
- [Schritt 2 – WLAN konfigurieren](#schritt-2--wlan-konfigurieren)
- [Schritt 3 – Projekt öffnen](#schritt-3--projekt-in-vs-code-öffnen)
- [Schritt 4 – Firmware flashen](#schritt-4--firmware-flashen)
- [Schritt 5 – IP‑Adresse finden](#schritt-5--ip-adresse-herausfinden)
- [Schritt 6 – Ersteinrichtung](#schritt-6--ersteinrichtung-im-web-interface)
- [OTA‑Updates](#-ota-firmware-update)
- [Fehlerbehebung](#-fehlerbehebung)

---

## ✅ Voraussetzungen

### Software

| Tool | Zweck | Download |
|---|---|---|
| **Visual Studio Code** | Code‑Editor | https://code.visualstudio.com/ |
| **PlatformIO Extension** | Build‑System für ESP32 | In VS Code: Extensions → „PlatformIO IDE" |

> Der ESP32‑S3‑DevKitC‑1 verwendet **native USB** – kein externer
> USB‑UART‑Adapter und kein Treiber nötig. Windows erkennt das Gerät
> direkt als COM‑Port.

### Hardware

Alle Bauteile verkabelt gemäß [HW.de.md](HW.de.md).

---

## Schritt 1 – Repository klonen

```bash
git clone https://github.com/Huehnerfluesterer/ESP-Huehnerklappe.git
cd ESP-Huehnerklappe
```

Oder als ZIP herunterladen (GitHub → **Code → Download ZIP**) und
entpacken.

---

## Schritt 2 – WLAN konfigurieren

Öffne die Datei `src/config.h` und trage deine WLAN‑Zugangsdaten ein:

```cpp
#define WIFI_SSID     "Dein_WLAN_Name"
#define WIFI_PASSWORD "Dein_WLAN_Passwort"
```

---

## Schritt 3 – Projekt in VS Code öffnen

1. VS Code starten
2. **Datei → Ordner öffnen** → Projektordner wählen
3. PlatformIO erkennt das Projekt automatisch anhand der
   `platformio.ini`
4. Beim ersten Öffnen werden alle Bibliotheken automatisch
   heruntergeladen:

| Bibliothek | Zweck |
|---|---|
| `Adafruit VEML7700` | Lichtsensor |
| `Adafruit BME280` | Temperatur/Feuchte‑Sensor |
| `Adafruit Unified Sensor` | Abhängigkeit von BME/VEML |
| `RTClib` | DS3231 + NTP‑Zeitfunktionen |
| `ArduinoJson` | JSON für Web & MQTT |
| `PubSubClient` | MQTT‑Client |
| `Adafruit NeoPixel` | (für StatusLED‑Stubs) |

Das Board‑Profil `esp32-s3-devkitc-1-n16r8` kommt aus dem Unterordner
`boards/`. Die wichtigsten PlatformIO‑Einstellungen:

```ini
[env:esp32-s3-devkitc-1]
platform               = espressif32
board                  = esp32-s3-devkitc-1-n16r8
framework              = arduino
board_build.flash_size = 16MB
board_build.partitions = partitions_16mb_ota.csv
board_build.filesystem = littlefs
monitor_speed          = 115200
upload_speed           = 921600
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DCORE_DEBUG_LEVEL=0
    -DHTTP_UPLOAD_BUFLEN=8192
```

---

## Schritt 4 – Firmware flashen

1. ESP32‑S3 per USB mit dem Computer verbinden
2. In VS Code auf das **→ Pfeil‑Symbol** unten klicken
   (**PlatformIO: Upload**)
3. Warten bis im Terminal erscheint:

```
Wrote 1147472 bytes (765755 compressed) at 0x00010000
Hash of data verified.
[SUCCESS]
```

**Serielle Ausgabe beobachten** (empfohlen beim ersten Start):

```
🐔 Hühnerklappe – FW 3.0.16
✅ Task-Watchdog aktiv (Timeout 30000 ms)
✅ LittleFS gemountet
✅ WLAN verbunden – Kanal 6  IP: 192.168.x.x
✅ RTC per NTP synchronisiert
🚀 Hühnerklappe gestartet – FW 3.0.16
```

> Baudrate: **115 200**

**Bei Upload‑Problemen:** `BOOT`‑Taste auf dem ESP32‑S3 gedrückt halten,
während der Upload startet – dann loslassen. Alternativ: `BOOT` halten,
`RESET` kurz drücken, `BOOT` loslassen, dann Upload starten.

---

## Schritt 5 – IP-Adresse herausfinden

Die IP‑Adresse erscheint im seriellen Monitor nach dem WLAN‑Connect.
Alternativ:

- **Router‑Oberfläche:** unter verbundenen Geräten nach
  `Huehnerklappe-ESP32` suchen (Hostname wird per `WiFi.setHostname`
  gesetzt)
- **mDNS:** im Browser `http://klappe.local` aufrufen (im gleichen
  Netzwerk)

---

## Schritt 6 – Ersteinrichtung im Web-Interface

### 6.1 – Motorpositionen einlernen

Beim ersten Start müssen die Motorlaufzeiten für Öffnen und Schließen
eingelernt werden:

1. Web‑Interface öffnen → **Erweitert → Kalibrierung** (`/calibration`)
2. **Einlernmodus starten**
3. Klappe in die Endposition fahren (per Taster oder Motor‑Up/Down)
4. Position bestätigen – Werte werden im EEPROM unter
   `openPosition` / `closePosition` gespeichert

Alternativ direkt über `/learn` bzw. `/learn-page`.

### 6.2 – Betriebsmodus wählen

Unter **`/settings`** können Öffnungs‑ und Schließ‑Modus getrennt
eingestellt werden. Die Firmware kennt intern zwei Automatik‑Modi:

| Modus | Beschreibung |
|---|---|
| `light` | Automatisch anhand Helligkeit (VEML7700) |
| `time`  | Automatisch anhand fester Uhrzeiten |

Ein **reiner Manuell‑Modus** ist kein eigener String, sondern ergibt
sich daraus, dass Öffnungen über Taster/Web/MQTT die Automatik für
5–15 Minuten pausieren (`manualOverrideUntil`). Die Automatik selbst
bleibt im gewählten Modus.

### 6.3 – Lux-Schwellwerte anpassen

Richtwerte für den Lichtsensor:

| Situation | Lux |
|---|---|
| Klare Nacht | < 1 lx |
| Tiefe Dämmerung | 1 – 10 lx |
| Schließen empfohlen | 10 – 50 lx |
| Öffnen empfohlen | 100 – 500 lx |
| Bewölkter Tag | 1 000 – 10 000 lx |
| Sonniger Tag | > 10 000 lx |

> **Hinweis:** Wird für beide Modi `light` gewählt, **muss** die
> Öffnungsschwelle größer sein als die Schließschwelle – sonst
> verweigert die Firmware das Speichern mit der Meldung *„Öffnen‑Lux
> muss größer sein als Schließen‑Lux"*.

### 6.4 – MQTT konfigurieren (optional)

Unter **`/mqtt`** eintragen:

| Feld | Beispiel |
|---|---|
| Host | `192.168.1.100` |
| Port | `1883` |
| Client‑ID | `huehnerklappe` |
| Base‑Topic | `huehnerklappe` |
| Benutzer / Passwort | optional |

Details siehe [mqtt.de.md](mqtt.de.md).

### 6.5 – Telegram einrichten (optional)

Unter **Erweitert → 📱 Telegram** (`/telegram`) Bot‑Token, Chat‑ID und
Alarm‑Deadline eintragen. Ablauf siehe [telegram.de.md](telegram.de.md).

### 6.6 – ESP-NOW-Geräte (optional)

Wenn ein Außensensor oder der Remote‑Relais‑Node verwendet werden,
unter **Erweitert → 📶 ESP‑NOW Geräte** (`/espnow`) aktivieren und
MAC‑Adressen eintragen. Details: [espnow.de.md](espnow.de.md).

### 6.7 – Blockadeerkennung (optional)

Unter **Erweitert → ⚡ Blockadeerkennung** (`/blockade`) Schwellwert
einstellen. Details: [blockade.de.md](blockade.de.md).

---

## ✅ Erstinbetriebnahme-Checkliste

- [ ] Hardware verkabelt gemäß [HW.de.md](HW.de.md)
- [ ] Relais‑Jumper auf **Active HIGH** gesteckt
- [ ] In `src/pins.h` `RELAY_ON HIGH` / `RELAY_OFF LOW` gesetzt (siehe
      Hinweis in HW.de.md)
- [ ] `src/config.h` mit WLAN‑Zugangsdaten befüllt
- [ ] `src/config.h` in `.gitignore` eingetragen
- [ ] Firmware geflasht, serieller Monitor zeigt
      `🚀 Hühnerklappe gestartet`
- [ ] IP‑Adresse notiert, Web‑Interface unter `http://klappe.local`
      erreichbar
- [ ] Einlernmodus abgeschlossen (Motorpositionen gespeichert)
- [ ] Betriebsmodus gewählt und gespeichert
- [ ] Lux‑Schwellwerte an Standort angepasst
- [ ] Testlauf: Tür manuell öffnen und schließen

---

## 🔧 OTA Firmware-Update

Nach der Erstinstallation können Updates **kabellos** eingespielt
werden:

1. Neue Firmware bauen: PlatformIO → **Build** →
   `.pio/build/esp32-s3-devkitc-1/firmware.bin`
2. Web‑Interface → **Erweitert → ⬆️ Firmware Update** (`/fw`)
3. `.bin`‑Datei auswählen und hochladen
4. ESP32‑S3 startet nach dem Update automatisch neu

Während des OTA‑Uploads geht die Firmware in einen **I/O Safe State**
(Motor aus, alle Ausgänge aus, MQTT offline) und bedient nur noch den
WebServer. Erst nach einem erfolgreichen Update wird mit `ESP.restart()`
neu gestartet.

---

## 🔍 Fehlerbehebung

### WLAN verbindet sich erst nach mehreren Resets

Die Firmware wartet beim Start bis zu 15 s auf eine WLAN‑Verbindung und
macht danach weiter (damit ESP‑NOW gar nicht erst mit dem falschen
Kanal startet). Wenn der Router beim ersten Boot langsam antwortet,
stellt der spätere `wifiWatchdog()` im Loop die Verbindung her.

### ESP32 resettet kurz nach dem Start

Der interne Task‑WDT hat 30 s Timeout. Hängt eine Funktion in `setup()`
länger, gibt es einen Panic‑Reset mit Reset‑Reason
`ESP_RST_TASK_WDT`. In der Log‑Ausgabe erscheinen bei einem
Watchdog‑Reset entsprechende Backtraces – meistens hilft schon ein
sauberer Neustart, andernfalls Netzteil/Brownout prüfen.

### WebUI nicht erreichbar obwohl WLAN verbunden

Im seriellen Log prüfen, ob eine echte IP geloggt wurde. Wenn die IP
`0.0.0.0` lautet, war WLAN zum Zeitpunkt des `server.begin()` noch
nicht verbunden – einfach den ESP einmal neu starten.

### VEML7700 nicht gefunden

I²C‑Verbindung prüfen: SDA = GPIO 8, SCL = GPIO 9. Spannung am
VEML7700 muss **3,3 V** sein. I²C‑Adresse: `0x10`.

Bei einem Hard‑Fehler des Sensors startet die Firmware jeden Tag um
**03:00 Uhr** automatisch neu, und – sofern Telegram aktiviert ist –
wird eine Fehlermeldung gepusht.

### Motor dreht nicht

- `ENA`‑Jumper am L298N entfernt?
- L298N mit 12 V versorgt?
- GPIO 13 korrekt verbunden?
- Blockadeerkennung zu empfindlich eingestellt?
  (siehe [blockade.de.md](blockade.de.md))

### Relais klickt beim Booten / schaltet invertiert

Jumper auf dem Relaismodul auf **Active HIGH** prüfen und in
`src/pins.h` `RELAY_ON HIGH` / `RELAY_OFF LOW` setzen. Bei Active‑LOW
(Werkseinstellung mancher Module) ziehen die Relais kurz an, wenn die
GPIOs beim Boot LOW sind.

### Build-Fehler auf Windows: `WiFi.h not found`

Prüfen, ob eine Datei `src/WiFi.cpp` im Projektordner liegt – diese
löschen. Windows unterscheidet keine Groß‑/Kleinschreibung, wodurch
ein Namenskonflikt mit dem Framework‑Header `<WiFi.h>` entsteht.

### Zeit springt nach dem Reset

Ohne DS3231 hält die Firmware die Uhrzeit nur per NTP. Nach einem
Neustart ohne Internet steht die Zeit initial auf
`2000-01-01 08:00:00` – in diesem Zustand werden alle zeitabhängigen
Automatiken übersprungen, bis NTP einmal erfolgreich war. Ein DS3231
(`0x68` am I²C‑Bus) schafft hier Abhilfe.
