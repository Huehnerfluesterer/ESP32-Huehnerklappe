# 📦 Installationsanleitung

🇩🇪 Deutsch

---

## Inhaltsverzeichnis

- [Voraussetzungen](#-voraussetzungen)
- [Schritt 1 – Repository klonen](#schritt-1--repository-klonen)
- [Schritt 2 – WLAN konfigurieren](#schritt-2--wlan-konfigurieren)
- [Schritt 3 – Projekt öffnen](#schritt-3--projekt-in-vs-code-öffnen)
- [Schritt 4 – Firmware flashen](#schritt-4--firmware-flashen)
- [Schritt 5 – IP-Adresse finden](#schritt-5--ip-adresse-herausfinden)
- [Schritt 6 – Ersteinrichtung](#schritt-6--ersteinrichtung-im-web-interface)
- [OTA-Updates](#-ota-firmware-update)
- [Fehlerbehebung](#-fehlerbehebung)

---

## ✅ Voraussetzungen

### Software

| Tool | Zweck | Download |
|---|---|---|
| **Visual Studio Code** | Code-Editor | [code.visualstudio.com](https://code.visualstudio.com/) |
| **PlatformIO Extension** | Build-System für ESP32 | In VS Code: Extensions → „PlatformIO IDE" |

> Der ESP32-S3-DevKitC-1 verwendet **native USB** – kein externer USB-UART-Adapter und kein Treiber nötig. Windows erkennt das Gerät direkt als COM-Port.

### Hardware

Alle Bauteile verkabelt gemäß [Hardware-Anleitung](HW.de.md).

---

## Schritt 1 – Repository klonen

```bash
git clone https://github.com/Huehnerfluesterer/ESP-Huehnerklappe.git
cd ESP-Huehnerklappe
```

Oder als ZIP herunterladen (GitHub → **Code → Download ZIP**) und entpacken.

---

## Schritt 2 – WLAN konfigurieren

Öffne die Datei `src/config.h` und trage deine WLAN-Zugangsdaten ein:

```cpp
#define WIFI_SSID     "Dein_WLAN_Name"
#define WIFI_PASSWORD "Dein_WLAN_Passwort"
```

> ⚠️ **Sicherheit:** Diese Datei enthält geheime Zugangsdaten.  
> In `.gitignore` eintragen **bevor** du das Projekt auf GitHub hochlädst:
>
> ```
> # .gitignore
> src/config.h
> ```

---

## Schritt 3 – Projekt in VS Code öffnen

1. VS Code starten
2. **Datei → Ordner öffnen** → Projektordner wählen
3. PlatformIO erkennt das Projekt automatisch anhand der `platformio.ini`
4. Beim ersten Öffnen werden alle Bibliotheken automatisch heruntergeladen:

| Bibliothek | Zweck |
|---|---|
| `Adafruit VEML7700` | Lichtsensor |
| `RTClib` | Zeitfunktionen (NTP) |
| `ArduinoJson` | JSON für Web & MQTT |
| `PubSubClient` | MQTT-Client |
| `Adafruit BME280` | Temperatur/Feuchte-Sensor |

---

## Schritt 4 – Firmware flashen

1. ESP32-S3 per USB mit dem Computer verbinden
2. In VS Code auf das **→ Pfeil-Symbol** unten klicken (**PlatformIO: Upload**)
3. Warten bis im Terminal erscheint:

```
Wrote 1147472 bytes (765755 compressed) at 0x00010000
Hash of data verified.
[SUCCESS]
```

**Serielle Ausgabe beobachten** (empfohlen beim ersten Start):

```
🐔 Hühnerklappe – FW 1.0.16
✅ LittleFS gemountet
✅ TPL5110 Hardware-Watchdog aktiv (GPIO 6)
✅ VEML7700 initialisiert
✅ WLAN verbunden – IP: 192.168.x.x
✅ RTC per NTP synchronisiert
🚀 Hühnerklappe gestartet – FW 1.0.16
```

> Baudrate: **115 200**

**Bei Upload-Problemen:**  
`BOOT`-Taste auf dem ESP32-S3 gedrückt halten, während der Upload startet – dann loslassen.

---

## Schritt 5 – IP-Adresse herausfinden

Die IP-Adresse erscheint im seriellen Monitor nach dem WLAN-Connect. Alternativ:

- **Router-Oberfläche:** Unter verbundenen Geräten nach `Huehnerklappe` suchen
- **mDNS:** Im Browser `http://klappe.local` aufrufen (im gleichen Netzwerk)

---

## Schritt 6 – Ersteinrichtung im Web-Interface

### 6.1 – Motorpositionen einlernen (Lern-Modus)

Beim ersten Start müssen die Motorlaufzeiten eingelernt werden:

1. Web-Interface öffnen → **`/learn`**
2. **Phase 1:** Auf „Start" klicken → Motor fährt Tür vollständig auf → „Position bestätigen"
3. **Phase 2:** Motor fährt Tür vollständig zu → „Position bestätigen"
4. Positionen werden im EEPROM dauerhaft gespeichert

---

### 6.2 – Betriebsmodus wählen

Unter **`/settings`**:

| Modus | Beschreibung |
|---|---|
| `lux` | Automatisch anhand Helligkeit (empfohlen) |
| `time` | Automatisch anhand fester Uhrzeiten |
| `manual` | Nur manuell über Taster / Web / MQTT |

---

### 6.3 – Lux-Schwellwerte anpassen

**Richtwerte:**

| Situation | Lux |
|---|---|
| Klare Nacht | < 1 lx |
| Tiefe Dämmerung | 1 – 10 lx |
| Schließen empfohlen | 10 – 50 lx |
| Öffnen empfohlen | 100 – 500 lx |
| Bewölkter Tag | 1 000 – 10 000 lx |
| Sonniger Tag | > 10 000 lx |

---

### 6.4 – MQTT konfigurieren (optional)

Unter **`/mqtt`** eintragen:

| Feld | Beispiel |
|---|---|
| Host | `192.168.1.100` |
| Port | `1883` |
| Client-ID | `huehnerklappe` |
| Base-Topic | `huehnerklappe` |

---

## ✅ Erstinbetriebnahme-Checkliste

- [ ] Hardware verkabelt gemäß [HW.de.md](HW.de.md)
- [ ] Relais-Jumper auf **Active HIGH** gesteckt
- [ ] `src/config.h` mit WLAN-Zugangsdaten befüllt
- [ ] `src/config.h` in `.gitignore` eingetragen
- [ ] Firmware geflasht, serieller Monitor zeigt `🚀 Hühnerklappe gestartet`
- [ ] IP-Adresse notiert, Web-Interface unter `http://klappe.local` erreichbar
- [ ] Lern-Modus abgeschlossen (Motorpositionen gespeichert)
- [ ] Betriebsmodus gewählt und gespeichert
- [ ] Lux-Schwellwerte an Standort angepasst
- [ ] Testlauf: Tür manuell öffnen und schließen
- [ ] Optional: TPL5110 Watchdog-Test durchgeführt (`curl -X POST http://<IP>/test/wdog`)

---

## 🔧 OTA Firmware-Update

Nach der Erstinstallation können Updates **kabellos** eingespielt werden:

1. Neue Firmware bauen: PlatformIO → **Build** → `.pio/build/esp32-s3-devkitc-1/firmware.bin`
2. Web-Interface öffnen → **`/fw`**
3. Datei auswählen und hochladen
4. ESP32-S3 startet nach dem Update automatisch neu

---

## 🔍 Fehlerbehebung

### WLAN verbindet sich erst nach mehreren Resets

Der Software-Watchdog hat 60 s Timeout, der WiFi-Wartezeit ist 30 s. Wenn der Router beim ersten Boot langsam antwortet, kann der erste Verbindungsversuch fehlschlagen – beim zweiten oder dritten Versuch klappt es.

### ESP32 resettet kurz nach dem Start

Der TPL5110-Watchdog hat ca. 50 s Timeout. Wenn WiFi sehr lange braucht, kann der Timer ablaufen. Lösung: DELAY-Widerstand auf 39 kΩ erhöhen (~65 s Timeout).

### WebUI nicht erreichbar obwohl WLAN verbunden

Kontrolliere ob die IP `0.0.0.0` im Log steht (WiFi war zum Zeitpunkt des `server.begin()` noch nicht verbunden). In diesem Fall den ESP einmal neu starten.

### VEML7700 nicht gefunden

I²C-Verbindung prüfen: SDA = GPIO 8, SCL = GPIO 9. Spannung am VEML7700 muss **3,3 V** sein. I²C-Adresse des VEML7700: `0x10`.

### Motor dreht nicht

ENA-Jumper am L298N entfernt? L298N mit 12 V versorgt? GPIO 13 korrekt verbunden?

### Relais klickt beim Booten

Jumper auf dem Relaismodul auf **Active HIGH** prüfen. Bei Active LOW (Werkseinstellung vieler Module) ziehen die Relais kurz an wenn GPIOs beim Boot LOW sind.

### Build-Fehler auf Windows: `WiFi.h not found`

Prüfen ob eine Datei `src/WiFi.cpp` im Projektordner liegt – diese löschen. Windows unterscheidet keine Groß-/Kleinschreibung, wodurch ein Namenskonflikt mit dem Framework-Header `<WiFi.h>` entsteht.
