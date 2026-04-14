# 🔌 Hardware & Verkabelung

🇩🇪 Deutsch

---

## Inhaltsverzeichnis

- [Benötigte Bauteile](#-benötigte-bauteile)
- [Pin-Übersicht ESP32-S3](#-pin-übersicht-esp32-s3)
- [Schritt-für-Schritt Verkabelung](#-schritt-für-schritt-verkabelung)
- [Steckverbindungen im Überblick](#-steckverbindungen-im-überblick)

---

## 🛒 Benötigte Bauteile

### Pflichtbauteile

| Bauteil | Beschreibung | Anzahl |
|---|---|---|
| **ESP32-S3-DevKitC-1-N16R8** | Mikrocontroller (16 MB Flash, 8 MB OPI-PSRAM) | 1 |
| **L298N** | Motorsteuermodul (Dual H-Brücke) | 1 |
| **Gleichstrommotor 12 V** | Getriebemotor passend zur Klappe | 1 |
| **VEML7700** | Lichtsensor-Breakout (I²C, 3,3 V) | 1 |
| **Relaismodul 5 V, 2-Kanal** | Mit Jumper auf **Active HIGH** gesteckt | 1 |
| **Netzteil 12 V / min. 2 A** | Für Motor und LED-Strip | 1 |
| **LM2596 Step-Down** | 12 V → 5 V für ESP32 | 1 |
| **Taster NO** | Kurzhubtaster, normalerweise offen | 3 |

### Optionale Bauteile

| Bauteil | Beschreibung | Anzahl |
|---|---|---|
| **IRLZ44N** | N-Kanal Logic-Level MOSFET für RGB+WW LED-Strip | 5 |
| **Widerstand 10 kΩ** | Gate Pull-down für jeden MOSFET | 5 |
| **RGBW+WW LED-Strip 12 V** | 5 Kanäle: R, G, B, W, WW | nach Bedarf |
| **ACS712 (5 A)** | Stromsensor für Motorblockade-Erkennung | 1 |
| **Endschalter / Mikroschalter** | Für genaue Positionserkennung | 2 |
| **BME280** | Temperatur/Feuchte-Sensor (lokal oder via ESP-NOW) | 1 |
| **Gehäuse IP65** | Wetterfester Anschlusskasten | 1 |

---

## 📍 Pin-Übersicht ESP32-S3

| ESP32-S3 GPIO | Funktion | Bauteil | Hinweis |
|---|---|---|---|
| **GPIO 15** | Motor IN1 | L298N IN1 | Richtungssteuerung |
| **GPIO 5** | Motor IN2 | L298N IN2 | Richtungssteuerung |
| **GPIO 13** | Motor ENA (PWM) | L298N ENA | Geschwindigkeit via LEDC |
| **GPIO 18** | Locklicht-Relais | Relais Kanal 1 | HIGH = AN (Active HIGH Jumper) |
| **GPIO 10** | Stalllicht-Relais | Relais Kanal 2 | HIGH = AN (Active HIGH Jumper) |
| **GPIO 39** | Taster Tür | Taster 1 | INPUT_PULLUP |
| **GPIO 41** | Taster Stalllicht | Taster 2 | INPUT_PULLUP |
| **GPIO 40** | Taster Rotlicht | Taster 3 | INPUT_PULLUP |
| **GPIO 7** | ACS712 analog | Stromsensor | ADC1, WiFi-sicher |
| **GPIO 8** | I²C SDA | VEML7700 | |
| **GPIO 9** | I²C SCL | VEML7700 | |
| **GPIO 14** | Endschalter AUF | Mikroschalter | LOW = Position erreicht |
| **GPIO 12** | Endschalter ZU | Mikroschalter | LOW = Position erreicht |
| **GPIO 4** | RGB Rot (PWM) | IRLZ44N Gate | 10 kΩ Pull-down |
| **GPIO 16** | RGB Grün (PWM) | IRLZ44N Gate | 10 kΩ Pull-down |
| **GPIO 17** | RGB Blau (PWM) | IRLZ44N Gate | 10 kΩ Pull-down |
| **GPIO 21** | RGB Weiß (PWM) | IRLZ44N Gate | 10 kΩ Pull-down |

---

## 🔧 Schritt-für-Schritt Verkabelung

> ⚠️ **Sicherheit:** Vor jeder Verkabelungsarbeit die Stromversorgung trennen.  
> ⚠️ **Spannung:** 12 V und 3,3 V niemals vertauschen – der ESP32-S3 verträgt maximal 3,3 V an den GPIO-Pins!

---

### 1 · Stromversorgung

```
12 V Netzteil (+) ──── L298N VSS (12 V Motoreingang)
                  ──── LED-Strip V+
                  ──── LM2596 IN+

12 V Netzteil (−) ──── Gemeinsame Masse (GND)

LM2596 OUT+ (5 V) ──── ESP32-S3 5V-Pin
LM2596 OUT− ──────── GND (gemeinsam)
```

> ⚠️ **Gemeinsamer GND ist Pflicht!** ESP32, L298N, MOSFETs und Netzteil müssen denselben GND-Bezugspunkt haben.

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

> ⚠️ Den **ENA-Jumper** auf dem L298N **entfernen**, sonst hat die PWM-Steuerung keinen Effekt!

---

### 3 · I²C-Bus (VEML7700)

```
ESP32-S3 GPIO 8 (SDA) ──── VEML7700 SDA
ESP32-S3 GPIO 9 (SCL) ──── VEML7700 SCL
ESP32-S3 3,3 V ────────── VEML7700 VCC  ← unbedingt 3,3 V!
GND ───────────────────── VEML7700 GND
```

> ⚠️ Der VEML7700 ist ein **3,3 V-Sensor**. Niemals an 5 V anschließen!

---

### 4 · Relais (Locklicht & Stalllicht)

Der Jumper auf dem Relaismodul muss auf **Active HIGH** gesteckt sein:

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

> **Active HIGH:** Relais zieht an wenn GPIO HIGH. Beim Bootvorgang bleiben GPIOs LOW → Relais bleiben offen → kein ungewolltes Schalten beim Start.

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

---

### 6 · RGBW+WW LED-Strip über MOSFET (5 Kanäle)

```
+12V ──────────────────────────── Strip +12V

Strip R  ──── Drain  IRLZ44N (R)
Strip G  ──── Drain  IRLZ44N (G)
Strip B  ──── Drain  IRLZ44N (B)
Strip W  ──── Drain  IRLZ44N (W)
Strip WW ──── Drain  IRLZ44N (WW)

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

> **Kein Optokoppler nötig!** Der IRLZ44N ist Logic-Level kompatibel (öffnet ab 2 V Gate) und schaltet direkt mit 3,3 V GPIO-Signal.

---

### 7 · Endschalter (optional)

```
Endschalter AUF   Pin 1 ──── ESP32-S3 GPIO 14
                  Pin 2 ──── GND

Endschalter ZU    Pin 1 ──── ESP32-S3 GPIO 12
                  Pin 2 ──── GND
```

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
│  GPIO  8 (SDA) ──── VEML7700 SDA         │
│  GPIO  9 (SCL) ──── VEML7700 SCL         │
│  GPIO 39 ────────── Taster Tür           │
│  GPIO 41 ────────── Taster Stalllicht    │
│  GPIO 40 ────────── Taster Rotlicht      │
│  GPIO 14 ────────── Endschalter ↑        │
│  GPIO 12 ────────── Endschalter ↓        │
│  GPIO  7 ────────── ACS712 OUT           │
│  GPIO  4 ──[10kΩ]── IRLZ44N Gate (R)    │
│  GPIO 16 ──[10kΩ]── IRLZ44N Gate (G)    │
│  GPIO 17 ──[10kΩ]── IRLZ44N Gate (B)    │
│  GPIO 21 ──[10kΩ]── IRLZ44N Gate (W)    │
│  GPIO  6 ────────── TPL5110 DONE         │
│  EN-Pin  ────────── TPL5110 DRV          │
│  3,3 V ──────────── VEML7700 VCC         │
│  GND ────────────── alle GNDs            │
└──────────────────────────────────────────┘
```
