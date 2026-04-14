# 🐔 Hühnerklappe – Firmware

Automatische Hühnerklappe auf Basis des **ESP32-S3-DevKitC-1-N16R8**.

---

## Hardware

| Komponente | Typ / Wert |
|---|---|
| Mikrocontroller | ESP32-S3-DevKitC-1-N16R8 |
| Flash | 16 MB Quad-SPI |
| PSRAM | 8 MB Octal-SPI (OPI) |
| Motorsteuerung | L298N Dual H-Bridge |
| Motor | 12 V DC Getriebemotor |
| Licht-Sensor | VEML7700 (I²C) |
| Temperatursensor | BME280 (lokal I²C oder via ESP-NOW) |
| Stromsensor | ACS712-5A |
| RGBW+WW LED-Strip | 12 V, 5 Kanäle, 5× IRLZ44N (Logic-Level MOSFET) |
| Relais Locklicht | 5 V, **HIGH-aktiv** (Jumper auf Active HIGH) |
| Relais Stalllicht | 5 V, **HIGH-aktiv** (Jumper auf Active HIGH) |
| Versorgung | 12 V DC → LM2596 5 V → onboard 3,3 V |

---

## Pin-Belegung ESP32-S3

> **Hinweis:** GPIO 26–32 sind intern für Flash belegt, GPIO 33–37 für OPI-PSRAM, GPIO 19/20 für native USB-CDC.  
> Auf dem ESP32-S3 gibt es **keine input-only GPIOs** mehr – alle Pins unterstützen `INPUT_PULLUP`.

| Signal | GPIO | Anmerkung |
|--------|------|-----------|
| MOTOR_IN1 | 15 | L298N Richtung A |
| MOTOR_IN2 | 5 | L298N Richtung B |
| MOTOR_ENA | 13 | L298N PWM Enable (LEDC) |
| RELAIS_PIN (Locklicht) | 18 | HIGH-aktiv (Jumper auf Active HIGH) |
| STALLLIGHT_RELAY_PIN | 10 | HIGH-aktiv (Jumper auf Active HIGH) |
| BUTTON_PIN (Klappe) | 39 | INPUT_PULLUP |
| STALL_BUTTON_PIN | 41 | INPUT_PULLUP |
| RED_BUTTON_PIN | 40 | INPUT_PULLUP |
| ACS712_PIN (Analog) | 7 | ADC1 – WiFi-sicher |
| I2C SDA | 8 | VEML7700 |
| I2C SCL | 9 | VEML7700 |
| LIMIT_OPEN_PIN | 14 | Endschalter Öffnen (optional) |
| LIMIT_CLOSE_PIN | 12 | Endschalter Schließen (optional) |
| RGB_PIN_R | 4 | IRLZ44N Gate, 10 kΩ Pull-down |
| RGB_PIN_G | 16 | IRLZ44N Gate, 10 kΩ Pull-down |
| RGB_PIN_B | 17 | IRLZ44N Gate, 10 kΩ Pull-down |
| RGB_PIN_W | 21 | IRLZ44N Gate, 10 kΩ Pull-down |
| RGB_PIN_WW | (konfigurierbar) | IRLZ44N Gate, 10 kΩ Pull-down |

---

## Relais-Konfiguration

Die Relaismodule sind auf **Active HIGH** konfiguriert (Jumper umgesteckt):

- `RELAY_ON = HIGH` → Relais zieht an wenn GPIO HIGH
- `RELAY_OFF = LOW`

Dies verhindert das ungewollte Anziehen der Relais beim Bootvorgang, da GPIOs beim Start LOW sind.

---

## RGBW+WW LED-Strip (5 Kanäle)

Der Strip hat 5 Kanäle: R, G, B, W (Kaltweiß), WW (Warmweiß).  
Jeder Kanal wird über einen **IRLZ44N** N-Kanal MOSFET geschaltet (Low-Side Switching):

```
+12V ────────────────────── Strip +12V (gemeinsam)

Strip R  ──── Drain │
Strip G  ──── Drain │  je ein IRLZ44N
Strip B  ──── Drain │  + 10 kΩ Pull-down Gate→GND
Strip W  ──── Drain │
Strip WW ──── Drain │

Source ──── GND (gemeinsam mit ESP32 und Netzteil)

ESP32 GPIO ──── Gate (10 kΩ Pull-down nach GND)
```

**Warum IRLZ44N?** Logic-Level kompatibel (öffnet ab 2 V Gate), kein Optokoppler, lineares Dimmen via PWM. Kein Kühlkörper nötig bis ca. 5 A pro Kanal.

---

## Hardware-Watchdog TPL5110

```
TPL5110 VDD  → 3,3 V
TPL5110 GND  → GND
TPL5110 DRV  → ESP32-S3 EN-Pin  (Reset-Leitung)
TPL5110 DONE → GPIO 6
33 kΩ an DELAY → GND  (≈ 50 s Timeout)
```

Der ESP32 sendet alle 20 s einen Puls an DONE. Bleibt der Puls aus (Loop hängt), resettet der TPL5110 den ESP32 nach ~50 s automatisch.

Test: `curl -X POST http://<IP>/test/wdog` – blockiert den Loop für 70 s, TPL5110 feuert nach ~50 s.

---

## StatusLED (deaktiviert)

Der ESP32-S3-N16R8 verwendet OPI-PSRAM (GPIO 33–37). Das RMT-Peripheral des ESP32-S3, das für WS2812-LEDs benötigt wird, kollidiert bei diesem Board mit dem OPI-PSRAM und verursacht WiFi-Abstürze. Die interne RGB-LED (GPIO 48) ist daher in der Firmware deaktiviert (`statusled.cpp` enthält nur leere Stubs).

Workaround: Board ohne OPI-PSRAM verwenden (z.B. N8R2 = 8 MB Flash, kein PSRAM), dann ist die WS2812-LED nutzbar.

---

## Partitionstabelle

```
partitions_16mb_ota.csv  (board_build.partitions)
```

| Partition | Typ | Größe |
|---|---|---|
| nvs | NVS | 20 KB |
| otadata | OTA-Data | 8 KB |
| app0 | OTA_0 | ~6,5 MB |
| app1 | OTA_1 | ~6,5 MB |
| spiffs | SPIFFS/LittleFS | ~1,5 MB |

> **Wichtig:** PlatformIO erwartet `board_build.filesystem = littlefs` und die Partition heißt intern `spiffs` – das ist korrekt und kein Fehler.

---

## Flashen

```bash
# PlatformIO CLI
pio run -t upload

# LittleFS-Partition flashen (Erstinstallation)
pio run -t uploadfs

# Bei Boot-Problemen
# BOOT-Taste gedrückt halten → Reset drücken → loslassen
```

---

## Konfiguration

Zugangsdaten in `src/config.h` eintragen (nicht ins Git committen!):

```cpp
#define WIFI_SSID     "dein_ssid"
#define WIFI_PASSWORD "dein_passwort"
```

`.gitignore` Eintrag:
```
src/config.h
```

---

## Teilprojekte

| Ordner | Board | Funktion |
|---|---|---|
| `klappe_new/` | ESP32-S3-DevKitC-1-N16R8 | Hauptsteuerung Hühnerklappe |
| `bme280_sender/` | ESP32 DevKit | BME280-Sensor → ESP-NOW Sender |
| `relay_esp/` | ESP32-C3-DevKitM-1 | Relais-Node (ESP-NOW Empfänger) |
