# 🔌 Hardware & Verkabelung

🇩🇪 Deutsch

---

## Inhaltsverzeichnis

- [Benötigte Bauteile](#-benötigte-bauteile)
- [Pin-Übersicht ESP32-S3](#-pin-übersicht-esp32-s3)
- [Schritt-für-Schritt Verkabelung](#-schritt-für-schritt-verkabelung)
- [Steckverbindungen im Überblick](#-steckverbindungen-im-überblick)
- [Gesperrte / belegte GPIOs](#-gesperrte--belegte-gpios)

---

## 🛒 Benötigte Bauteile

### Pflichtbauteile

| Bauteil | Beschreibung | Anzahl |
|---|---|---|
| **ESP32‑S3‑DevKitC‑1‑N16R8** | Mikrocontroller (16 MB Flash, 8 MB OPI‑PSRAM) | 1 |
| **L298N** | Motorsteuermodul (Dual H‑Brücke) | 1 |
| **Gleichstrommotor 12 V** | Getriebemotor passend zur Klappe | 1 |
| **VEML7700** | Lichtsensor‑Breakout (I²C, 3,3 V, Adresse `0x10`) | 1 |
| **Relaismodul 5 V, 2‑Kanal** | Jumper auf **Active HIGH** gesteckt | 1 |
| **Netzteil 12 V / min. 2 A** | Für Motor und LED‑Strip | 1 |
| **LM2596 Step‑Down** | 12 V → 5 V für ESP32 | 1 |
| **Taster NO** | Kurzhubtaster, normalerweise offen | 3 |

### Optionale Bauteile

| Bauteil | Beschreibung | Anzahl |
|---|---|---|
| **DS3231** | Echtzeituhr‑Breakout, I²C | 1 |
| **IRLZ44N** | N‑Kanal Logic‑Level MOSFET für RGBW LED‑Strip | 4 |
| **Widerstand 10 kΩ** | Gate Pull‑down für jeden MOSFET | 4 |
| **RGBW LED‑Strip 12 V** | 4 Kanäle: R, G, B, W | nach Bedarf |
| **ACS712 (5 A)** | Stromsensor für Blockadeerkennung | 1 |
| **Endschalter / Mikroschalter** | Für genaue Positionserkennung | 2 |
| **BME280** | Temperatur/Feuchte‑Sensor (lokal I²C oder via ESP‑NOW) | 1 |
| **Zweiter ESP32 + BME280** | Außensensor per ESP‑NOW (siehe [espnow.de.md](espnow.de.md)) | 1 |
| **ESP32‑C3 Relais‑Node** | Remote‑Relais per ESP‑NOW (siehe [espnow.de.md](espnow.de.md)) | 1 |
| **Gehäuse IP65** | Wetterfester Anschlusskasten | 1 |

---

## 📍 Pin-Übersicht ESP32-S3

| ESP32‑S3 GPIO | Funktion | Bauteil | Hinweis |
|---|---|---|---|
| **GPIO 15** | `MOTOR_IN1` | L298N IN1 | Richtungssteuerung |
| **GPIO 5**  | `MOTOR_IN2` | L298N IN2 | Richtungssteuerung |
| **GPIO 13** | `MOTOR_ENA` (PWM) | L298N ENA | Geschwindigkeit via LEDC |
| **GPIO 18** | Locklicht‑Relais | Relais Kanal 1 | **HIGH = AN** (Active HIGH) |
| **GPIO 10** | Stalllicht‑Relais | Relais Kanal 2 | **HIGH = AN** (Active HIGH) |
| **GPIO 39** | Taster Tür | Taster 1 | `INPUT_PULLUP` |
| **GPIO 41** | Taster Stalllicht | Taster 2 | `INPUT_PULLUP` |
| **GPIO 40** | Taster Rotlicht | Taster 3 | `INPUT_PULLUP` |
| **GPIO 7**  | `ACS712_PIN` | Stromsensor | ADC1, WiFi‑sicher |
| **GPIO 8**  | I²C SDA | VEML7700 + DS3231 | |
| **GPIO 9**  | I²C SCL | VEML7700 + DS3231 | |
| **GPIO 14** | `LIMIT_OPEN_PIN` | Endschalter Auf | `INPUT_PULLUP`, LOW = erreicht |
| **GPIO 12** | `LIMIT_CLOSE_PIN` | Endschalter Zu | `INPUT_PULLUP`, LOW = erreicht |
| **GPIO 4**  | `RGB_PIN_R` (PWM) | IRLZ44N Gate | 10 kΩ Pull‑down |
| **GPIO 16** | `RGB_PIN_G` (PWM) | IRLZ44N Gate | 10 kΩ Pull‑down |
| **GPIO 17** | `RGB_PIN_B` (PWM) | IRLZ44N Gate | 10 kΩ Pull‑down |
| **GPIO 21** | `RGB_PIN_W` (PWM) | IRLZ44N Gate | 10 kΩ Pull‑down |

---

## 🔧 Schritt-für-Schritt Verkabelung

> ⚠️ **Sicherheit:** Vor jeder Verkabelungsarbeit die Stromversorgung
> trennen.
>
> ⚠️ **Spannung:** 12 V und 3,3 V niemals vertauschen – der ESP32‑S3
> verträgt maximal 3,3 V an den GPIO‑Pins.

---

### 1 · Stromversorgung

```
12 V Netzteil (+) ──── L298N VSS (12 V Motoreingang)
                  ──── LED-Strip V+
                  ──── LM2596 IN+

12 V Netzteil (−) ──── Gemeinsame Masse (GND)

LM2596 OUT+ (5 V) ──── ESP32-S3 5V-Pin
LM2596 OUT−       ──── GND (gemeinsam)
```

> ⚠️ **Gemeinsamer GND ist Pflicht.** ESP32, L298N, MOSFETs und
> Netzteil müssen denselben GND‑Bezugspunkt haben.

---

### 2 · Motor (L298N)

```
L298N IN1  ──── ESP32-S3 GPIO 15
L298N IN2  ──── ESP32-S3 GPIO 5
L298N ENA  ──── ESP32-S3 GPIO 13
L298N OUT1 ──── Motor (+)
L298N OUT2 ──── Motor (−)
L298N GND  ──── Gemeinsame Masse
L298N VSS  ──── 12 V
```

> ⚠️ Den **ENA‑Jumper** auf dem L298N **entfernen**, sonst hat die
> PWM‑Steuerung keinen Effekt.

---

### 3 · I²C-Bus (VEML7700 + DS3231 + optional BME280)

```
ESP32-S3 GPIO 8 (SDA) ──── VEML7700 SDA ──── DS3231 SDA ──── BME280 SDA
ESP32-S3 GPIO 9 (SCL) ──── VEML7700 SCL ──── DS3231 SCL ──── BME280 SCL
ESP32-S3 3,3 V        ──── VEML7700 VCC ──── DS3231 VCC ──── BME280 VCC
GND                   ──── VEML7700 GND ──── DS3231 GND ──── BME280 GND
```

> ⚠️ VEML7700, DS3231 und BME280 sind **3,3 V‑Bauteile**. Niemals an
> 5 V anschließen.

I²C‑Adressen der Bauteile auf dem Bus:

| Bauteil | Adresse |
|---|---|
| VEML7700 | `0x10` |
| DS3231 | `0x68` |
| BME280 | `0x76` (SDO → GND) oder `0x77` (SDO → VCC) |

Pull‑up‑Widerstände (4,7 kΩ–10 kΩ) sind üblicherweise bereits auf den
Breakout‑Boards vorhanden. Wenn mehrere Module am selben Bus hängen,
reicht **ein** Pull‑up‑Paar.

Der Bus wird in `main.cpp` mit 100 kHz und 20 ms Timeout initialisiert:

```cpp
Wire.begin(I2C_SDA, I2C_SCL);
Wire.setClock(100000);
Wire.setTimeOut(20);
```

---

### 4 · Relais (Locklicht & Stalllicht)

Der Jumper auf dem Relaismodul **muss auf Active HIGH** gesteckt sein:

```
Relais IN1 ──── ESP32-S3 GPIO 18   (Locklicht,  HIGH = AN)
Relais IN2 ──── ESP32-S3 GPIO 10   (Stalllicht, HIGH = AN)
Relais VCC ──── 5 V
Relais GND ──── GND

Relais 1 COM ──── 12 V (+)
Relais 1 NO  ──── Locklicht (+)
Locklicht (−) ─── GND

Relais 2 COM ──── 12 V (+)
Relais 2 NO  ──── Stalllicht (+)
Stalllicht (−) ── GND
```

---

### 5 · Taster

```
Taster 1 (Tür)        Pin 1 ──── ESP32-S3 GPIO 39
                      Pin 2 ──── GND

Taster 2 (Stalllicht) Pin 1 ──── ESP32-S3 GPIO 41
                      Pin 2 ──── GND

Taster 3 (Rotlicht)   Pin 1 ──── ESP32-S3 GPIO 40
                      Pin 2 ──── GND
```

Alle drei Taster werden intern per `INPUT_PULLUP` gegen 3,3 V gezogen.
Ein externer Pull‑up‑Widerstand ist nicht erforderlich.

---

### 6 · RGBW LED-Strip über MOSFET (4 Kanäle)

```
+12V ──────────────────────────── Strip +12V

Strip R  ──── Drain  IRLZ44N (R)
Strip G  ──── Drain  IRLZ44N (G)
Strip B  ──── Drain  IRLZ44N (B)
Strip W  ──── Drain  IRLZ44N (W)

IRLZ44N Source ──── GND (gemeinsam)

ESP32-S3 GPIO 4  ──[10 kΩ nach GND]──── Gate IRLZ44N (R)
ESP32-S3 GPIO 16 ──[10 kΩ nach GND]──── Gate IRLZ44N (G)
ESP32-S3 GPIO 17 ──[10 kΩ nach GND]──── Gate IRLZ44N (B)
ESP32-S3 GPIO 21 ──[10 kΩ nach GND]──── Gate IRLZ44N (W)
```

**Schaltbild eines Kanals:**

```
ESP32 GPIO ────────── Gate ┐
           10 kΩ           IRLZ44N
           nach GND   Drain ──── Strip-Kanal (von +12V)
                      Source ─── GND
```

> **Kein Optokoppler nötig.** Der IRLZ44N ist Logic‑Level‑kompatibel
> (öffnet ab ca. 2 V Gate‑Spannung) und schaltet direkt mit dem 3,3 V
> GPIO‑Signal.

---

### 7 · ACS712 Stromsensor (optional, für Blockadeerkennung)

```
ACS712 VCC ──── 5 V
ACS712 GND ──── GND
ACS712 OUT ──── (Spannungsteiler ~2/3) ──── ESP32-S3 GPIO 7
Motor-Leitung ─ ACS712 IP+ / IP−
```

Der Ausgang des ACS712 liegt ratiometrisch bei `VCC/2` (≈ 2,5 V bei
5 V). Da der ADC des ESP32 nur bis 3,3 V misst, ist ein Spannungsteiler
auf ca. **2/3** erforderlich (im Code hart kodiert: `20 kΩ` oben,
`30 kΩ` unten, siehe `handleBlockade()`).

Einstellung und Kalibrierung: siehe [blockade.de.md](blockade.de.md).

---

### 8 · Endschalter (optional)

```
Endschalter AUF   Pin 1 ──── ESP32-S3 GPIO 14
                  Pin 2 ──── GND

Endschalter ZU    Pin 1 ──── ESP32-S3 GPIO 12
                  Pin 2 ──── GND
```

Beide Endschalter werden intern per `INPUT_PULLUP` gezogen (LOW =
Position erreicht). Die Nutzung wird im Web‑Interface unter
**Kalibrierung** aktiviert (`/set-limit-switches`).

---

## 📊 Steckverbindungen im Überblick

```
┌──────────────────────────────────────────┐
│         ESP32-S3-DevKitC-1-N16R8         │
│                                          │
│  GPIO 15 ────────── L298N IN1            │
│  GPIO  5 ────────── L298N IN2            │
│  GPIO 13 ────────── L298N ENA (PWM)      │
│  GPIO 18 ────────── Relais IN1 (Lock)    │
│  GPIO 10 ────────── Relais IN2 (Stall)   │
│  GPIO  8 (SDA) ──── VEML + DS3231 + BME  │
│  GPIO  9 (SCL) ──── VEML + DS3231 + BME  │
│  GPIO 39 ────────── Taster Tür           │
│  GPIO 41 ────────── Taster Stalllicht    │
│  GPIO 40 ────────── Taster Rotlicht      │
│  GPIO 14 ────────── Endschalter ↑        │
│  GPIO 12 ────────── Endschalter ↓        │
│  GPIO  7 ────────── ACS712 OUT (Teiler)  │
│  GPIO  4 ──[10kΩ]── IRLZ44N Gate (R)     │
│  GPIO 16 ──[10kΩ]── IRLZ44N Gate (G)     │
│  GPIO 17 ──[10kΩ]── IRLZ44N Gate (B)     │
│  GPIO 21 ──[10kΩ]── IRLZ44N Gate (W)     │
│  3,3 V ──────────── VEML + DS3231 VCC    │
│  GND ────────────── alle GNDs            │
└──────────────────────────────────────────┘
```

---

## 🚫 Gesperrte / belegte GPIOs

Auf dem **ESP32‑S3‑DevKitC‑1‑N16R8** sind folgende GPIOs **nicht frei
verwendbar**:

| GPIO | Funktion |
|---|---|
| 19, 20 | Native USB‑CDC (D+ / D−) |
| 22–25 | **Existieren auf dem S3 nicht** |
| 26–32 | Interner Quad‑SPI Flash |
| 33–37 | Interner Octal‑SPI PSRAM (OPI) |
| 48 | On‑board WS2812 (mit OPI‑PSRAM unbrauchbar, siehe README) |

GPIO 6 ist frei – die Firmware nutzt diesen Pin **nicht** mehr.
In älteren Versionen war hier der DONE‑Pin eines TPL5110 Hardware‑
Watchdogs angeschlossen, der durch den internen `esp_task_wdt` ersetzt
wurde.
