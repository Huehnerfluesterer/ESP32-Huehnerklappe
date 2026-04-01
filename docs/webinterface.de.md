# 🌐 Web-Interface

🇩🇪 Deutsch

---

## Übersicht der Seiten

| URL | Beschreibung |
|---|---|
| `/` | Startseite / Dashboard |
| `/settings` | Betriebseinstellungen |
| `/rgb` | Lichtfarbe & Helligkeit |
| `/mqtt` | MQTT-Konfiguration |
| `/log` | Ereignis-Logbuch |
| `/learn` | Lern-Modus (Motorpositionen) |
| `/systemtest` | System-Status |
| `/fw` | OTA Firmware-Update |
| `/simulation` | Simulationsmodus |
| `/test/wdog` | TPL5110 Watchdog-Test (POST) |

---

## Startseite (`/`)

Das Dashboard zeigt den aktuellen Systemstatus in Echtzeit (automatische Aktualisierung alle 3 s):

- Datum und Uhrzeit (NTP-synchronisiert)
- Klappenstatus (Offen / Geschlossen / In Bewegung)
- Aktueller Lux-Wert, Lux-Rate, Trend
- Licht-Status (Locklicht, Stalllicht, Rotlicht)
- WLAN-Signalstärke
- Firmware-Version, Uptime

**Schnellaktionen:**
- Klappe öffnen / schließen (Toggle)
- Locklicht ein/aus
- Stalllicht ein/aus
- Rotlicht ein/aus

Das Huhn-Logo im Header ist das eigene PWA-Icon (`/icon192`).

---

## Lichtfarbe & Helligkeit (`/rgb`)

- **Locklicht Farbe:** Colorpicker + manuelle RGB+W-Eingabe + Schnellfarben
- **Live-Vorschau:** Farbe und Helligkeit ändern sich sofort beim Schieben (80 ms Debounce via `/live-rgb`)
- **Helligkeit:** Slider 1–255, wirkt live auf aktives Locklicht
- **Rotlicht Helligkeit:** Separater Slider, wirkt sofort auch wenn Rotlicht aktiv ist

---

## Betriebseinstellungen (`/settings`)

| Einstellung | Beschreibung |
|---|---|
| Öffnen-Modus | `lux` / `time` / `manual` |
| Schließen-Modus | `lux` / `time` / `manual` |
| Öffnungszeit | Uhrzeit für Zeitsteuerung |
| Schließzeit | Uhrzeit für Zeitsteuerung |
| Lux-Öffnungsschwelle | Helligkeit in lx für Öffnung |
| Lux-Schließschwelle | Helligkeit in lx für Schließung |
| Vorlicht Öffnung | Minuten Locklicht vor Öffnung |
| Nachlicht Öffnung | Minuten Locklicht nach Öffnung |
| Vorlicht Schließung | Minuten Locklicht vor Schließung |
| Nachlicht Schließung | Minuten Locklicht nach Schließung |
| Motordauer Öffnen | Laufzeit in ms (aus Lern-Modus) |
| Motordauer Schließen | Laufzeit in ms (aus Lern-Modus) |

---

## Simulationsmodus (`/simulation`)

Ermöglicht das Testen der Automatik-Logik ohne echte Sensordaten:

- Lux-Wert manuell setzen
- Zeit simulieren
- Alle Automatik-Entscheidungen werden wie im Normalbetrieb ausgeführt
- Nützlich zum Testen von Schwellwerten und Zeitfenstern

---

## OTA Firmware-Update (`/fw`)

1. In PlatformIO: **Build** → `.pio/build/esp32-s3-devkitc-1/firmware.bin`
2. Seite `/fw` öffnen
3. `.bin`-Datei auswählen und hochladen
4. ESP32-S3 startet automatisch neu

> Während des OTA-Updates läuft nur `server.handleClient()` – Motor gestoppt, alle Ausgänge aus.

---

## TPL5110 Watchdog-Test (`/test/wdog`)

Nur per POST-Request erreichbar:

```bash
# Windows PowerShell
curl.exe -X POST http://192.168.x.x/test/wdog

# Linux/macOS
curl -X POST http://192.168.x.x/test/wdog
```

Der ESP blockiert danach 70 s ohne Watchdog-Pulse. Der TPL5110 resettet nach ~50 s.
