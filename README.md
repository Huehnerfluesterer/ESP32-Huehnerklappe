# 🐔 Hühnerklappe – Firmware

Automatische Hühnerklappe auf Basis des **ESP32‑S3‑DevKitC‑1‑N16R8**.
Lichtsensor‑ und Zeitsteuerung, RGBW‑Stalllicht, Blockadeerkennung,
Web‑Oberfläche als PWA, MQTT, Telegram‑Benachrichtigungen und
ESP‑NOW‑Anbindung für Außensensor und Remote‑Relais.

**Aktuelle Firmware‑Version:** `3.0.16` (siehe `src/main.cpp`)

---

## Dokumentation

| Dokument | Inhalt |
|---|---|
| [docs/HW.de.md](docs/HW.de.md) | Hardware, Pin‑Belegung, Verkabelung |
| [docs/installation.de.md](docs/installation.de.md) | Installation, Flashen, Ersteinrichtung |
| [docs/architecture.de.md](docs/architecture.de.md) | Software‑Architektur, Loop, Automatik |
| [docs/webinterface.de.md](docs/webinterface.de.md) | Alle Web‑Seiten und API‑Endpoints |
| [docs/mqtt.de.md](docs/mqtt.de.md) | MQTT‑Topics, Payloads, Beispiele |
| [docs/telegram.de.md](docs/telegram.de.md) | Telegram‑Benachrichtigungen |
| [docs/espnow.de.md](docs/espnow.de.md) | ESP‑NOW Außensensor + Relais‑Node |
| [docs/blockade.de.md](docs/blockade.de.md) | Blockadeerkennung über ACS712 |

---

## Hardware-Überblick

| Komponente | Typ / Wert |
|---|---|
| Mikrocontroller | ESP32‑S3‑DevKitC‑1‑N16R8 |
| Flash | 16 MB Quad‑SPI |
| PSRAM | 8 MB Octal‑SPI (OPI) |
| Motorsteuerung | L298N Dual H‑Bridge |
| Motor | 12 V DC Getriebemotor |
| Licht‑Sensor | VEML7700 (I²C, 3,3 V, Adresse `0x10`) |
| Echtzeit‑Uhr | DS3231 (I²C, optional, NTP als Fallback) |
| Temperatur/Feuchte | BME280 (lokal I²C **oder** via ESP‑NOW) |
| Stromsensor | ACS712‑5A (Blockadeerkennung) |
| RGBW LED‑Strip | 12 V, 4 Kanäle (R, G, B, W) über N‑Kanal MOSFETs |
| Relais Locklicht | 5 V, **Active HIGH** |
| Relais Stalllicht | 5 V, **Active HIGH** |
| Versorgung | 12 V DC → LM2596 5 V → onboard 3,3 V |

Details siehe [docs/HW.de.md](docs/HW.de.md).

---

## Pin-Belegung ESP32-S3

> **Hinweis:** Auf dem N16R8 sind GPIO 26–32 für Flash, GPIO 33–37 für
> OPI‑PSRAM und GPIO 19/20 für native USB‑CDC belegt und dürfen nicht
> anderweitig verwendet werden. GPIO 22–25 existieren auf dem S3 nicht.

| Signal | GPIO | Anmerkung |
|--------|------|-----------|
| `MOTOR_IN1` | 15 | L298N Richtung A |
| `MOTOR_IN2` | 5 | L298N Richtung B |
| `MOTOR_ENA` | 13 | L298N PWM Enable (LEDC) |
| `RELAIS_PIN` (Locklicht) | 18 | Relais Kanal 1 |
| `STALLLIGHT_RELAY_PIN` | 10 | Relais Kanal 2 |
| `BUTTON_PIN` (Klappe) | 39 | INPUT_PULLUP |
| `STALL_BUTTON_PIN` | 41 | INPUT_PULLUP |
| `RED_BUTTON_PIN` | 40 | INPUT_PULLUP |
| `ACS712_PIN` | 7 | ADC1 – WiFi‑sicher |
| `I2C_SDA` | 8 | VEML7700 + DS3231 + (BME280) |
| `I2C_SCL` | 9 | VEML7700 + DS3231 + (BME280) |
| `LIMIT_OPEN_PIN` | 14 | Endschalter Öffnen (optional) |
| `LIMIT_CLOSE_PIN` | 12 | Endschalter Schließen (optional) |
| `RGB_PIN_R` | 4 | IRLZ44N Gate, 10 kΩ Pull‑down |
| `RGB_PIN_G` | 16 | IRLZ44N Gate, 10 kΩ Pull‑down |
| `RGB_PIN_B` | 17 | IRLZ44N Gate, 10 kΩ Pull‑down |
| `RGB_PIN_W` | 21 | IRLZ44N Gate, 10 kΩ Pull‑down |

---

## Relais – Active LOW

Die verbauten Relaismodule sind per Jumper auf **Active HIGH**
konfiguriert. Die Relais ziehen an, wenn der zugehörige GPIO HIGH
ausgibt.

#define RELAY_ON  HIGH
#define RELAY_OFF LOW

---

## RGBW LED-Strip (4 Kanäle)

Der Strip hat die vier Kanäle **R, G, B, W**. Jeder Kanal wird über
einen **IRLZ44N** N‑Kanal MOSFET low‑side geschaltet:

```
+12 V ──────────────────────── Strip +12 V (gemeinsam)

Strip R  ──── Drain │  IRLZ44N
Strip G  ──── Drain │  + 10 kΩ Pull‑down Gate → GND
Strip B  ──── Drain │
Strip W  ──── Drain │

Source ──── GND (gemeinsam mit ESP32 und Netzteil)

ESP32 GPIO ──── Gate (10 kΩ Pull‑down nach GND)
```

**Warum IRLZ44N?** Logic‑Level‑kompatibel (öffnet ab ca. 2 V
Gate‑Spannung), keine Treiberstufe nötig, lineares Dimmen per LEDC‑PWM
direkt am 3,3 V GPIO. Kein Kühlkörper bis ca. 5 A pro Kanal.

Farbe und Helligkeit werden im EEPROM persistiert (5 Bytes:
`r, g, b, w, brightness`) und können über `/rgb` im Web‑Interface live
geändert werden.

---

## Watchdog

Die Firmware verwendet den **internen ESP32 Task‑Watchdog**
(`esp_task_wdt`) mit einem Timeout von 30 s. Wird `wdogFeed()` im Loop
länger als 30 s nicht aufgerufen, löst der Watchdog einen sauberen
Panic‑Reset aus (Reset‑Reason: `ESP_RST_TASK_WDT`).

> Die ursprüngliche Variante mit externem TPL5110 Hardware‑Watchdog an
> GPIO 6 ist **nicht mehr Bestandteil** der aktuellen Firmware. Die
> Init‑Funktion heißt aus Kompatibilitätsgründen noch `tpl5110Init()`,
> startet aber den internen Task‑WDT. GPIO 6 ist damit frei.

Nach einem Watchdog‑Reset schickt die Firmware, sofern Telegram
aktiviert ist, automatisch eine Nachricht (siehe
[docs/telegram.de.md](docs/telegram.de.md)).

---

## StatusLED

Der ESP32‑S3‑N16R8 verwendet OPI‑PSRAM (GPIO 33–37). Das RMT‑Peripheral,
das für die on‑board WS2812 an GPIO 48 benötigt wird, kollidiert bei
diesem Board mit dem OPI‑PSRAM und verursacht WiFi‑Abstürze. Die
`statusled.cpp` enthält deshalb nur leere Stubs.

Workaround: Board ohne OPI‑PSRAM verwenden (z. B. N8R2 = 8 MB Flash,
kein PSRAM), dann ist die WS2812 nutzbar.

---

## Partitionstabelle

`partitions_16mb_ota.csv` (als `board_build.partitions`):

| Partition | Typ | Größe |
|---|---|---|
| nvs | NVS | 20 KB |
| otadata | OTA‑Data | 8 KB |
| app0 | OTA_0 | ~6,5 MB |
| app1 | OTA_1 | ~6,5 MB |
| spiffs | LittleFS | ~1,5 MB |

> **Wichtig:** PlatformIO verwendet `board_build.filesystem = littlefs`.
> Die Partition heißt intern weiterhin `spiffs` – das ist korrekt und
> kein Fehler.

---

## Flashen

```bash
# Firmware
pio run -t upload

# LittleFS-Partition (nur bei Erstinstallation nötig)
pio run -t uploadfs
```

Bei Upload‑Problemen: **BOOT**‑Taste am DevKit gedrückt halten,
**RESET** drücken, loslassen und dann den Upload starten.

Serieller Monitor: **115200 baud**.

---

## Konfiguration

WLAN‑Zugangsdaten in `src/config.h`:

```cpp
#define WIFI_SSID     "dein_ssid"
#define WIFI_PASSWORD "dein_passwort"
```

> ⚠️ Diese Datei enthält Geheimnisse und muss in `.gitignore`:
>
> ```
> src/config.h
> ```
>
> Alle weiteren Einstellungen (MQTT, Telegram, RGB, ESP‑NOW‑MACs,
> Blockade, Motorpositionen, Theme) werden zur Laufzeit über die
> Web‑Oberfläche gesetzt und im EEPROM/NVS persistiert.

---

## Teilprojekte

Das Repository enthält neben der Hauptfirmware zwei kleine
Companion‑Sketches für ESP‑NOW:

| Ordner | Board | Funktion |
|---|---|---|
| `./` (`src/`) | ESP32‑S3‑DevKitC‑1‑N16R8 | Hauptsteuerung Hühnerklappe |
| `ESP-NOW/bme280_sender/` | ESP32 DevKit | BME280 → ESP‑NOW Sender |
| `ESP-NOW/relay_esp/` | ESP32‑C3‑DevKitM‑1 | Remote‑Relais (ESP‑NOW + Web + MQTT) |

Details siehe [docs/espnow.de.md](docs/espnow.de.md).
