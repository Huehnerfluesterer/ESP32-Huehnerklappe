# 🧠 System-Architektur

🇩🇪 Deutsch

---

## Inhaltsverzeichnis

- [Überblick](#-überblick)
- [Projektstruktur](#-projektstruktur)
- [Lux-Verarbeitungs-Pipeline](#-lux-verarbeitungs-pipeline)
- [Zustandsmaschinen](#-zustandsmaschinen)
- [Automatik-Logik](#-automatik-logik)
- [Loop-Architektur](#-loop-architektur)

---

## 🗺 Überblick

```
┌─────────────────────────────────────────────────────────────────┐
│                          main.cpp                               │
│          Setup · Loop · WebServer-Routen · OTA                  │
└───────┬──────────────────────────────────────────────┬──────────┘
        │                                              │
   ┌────▼─────┐  ┌──────────┐  ┌──────────┐  ┌───────▼──────┐
   │  logic   │  │   lux    │  │  motor   │  │     web/     │
   │ Automatik│  │ VEML7700 │  │  L298N   │  │  Webserver   │
   └────┬─────┘  └──────────┘  └──────────┘  └──────────────┘
        │
   ┌────▼─────┐  ┌──────────┐  ┌──────────┐  ┌──────────────┐
   │  light   │  │   door   │  │   mqtt   │  │   storage    │
   │  Licht   │  │  Taster  │  │  MQTT    │  │EEPROM/LittleFS│
   └──────────┘  └──────────┘  └──────────┘  └──────────────┘
        │
   ┌────▼─────┐  ┌──────────┐  ┌──────────┐
   │  system  │  │  logger  │  │  wlan    │
   │  Health  │  │ Logbuch  │  │ Watchdog │
   └──────────┘  └──────────┘  └──────────┘
```

---

## 📁 Projektstruktur

```
ESP-Huehnerklappe/
├── platformio.ini              # Build-Konfiguration, Bibliotheken
├── partitions_16mb_ota.csv     # Partitionstabelle 16 MB Flash
└── src/
    ├── config.h                # ⚠️ WLAN-Zugangsdaten (in .gitignore!)
    ├── pins.h                  # Alle GPIO-Pin-Definitionen
    ├── types.h                 # Enums (DoorPhase, MotorState, LightState)
    │                           # und Structs (Settings, MqttSettings)
    ├── main.cpp                # Setup, Loop, alle WebServer-Routen, OTA
    ├── motor.cpp / motor.h     # L298N-Ansteuerung via LEDC-PWM
    ├── door.cpp  / door.h      # Türzustand, Taster-Handling
    ├── lux.cpp   / lux.h       # VEML7700: Sensor, Filter, Trend, Recovery
    ├── light.cpp / light.h     # Licht-Automatik, RGB-PWM, Dimmer
    ├── logic.cpp / logic.h     # Automatik-Entscheidungslogik
    ├── mqtt.cpp  / mqtt.h      # PubSubClient-Wrapper, Topics, Publish
    ├── wlan.cpp  / wlan.h      # WLAN-Verbindung, Watchdog, Reconnect
    ├── storage.cpp / storage.h # EEPROM/LittleFS Lesen/Schreiben
    ├── logger.cpp  / logger.h  # Ringpuffer-Logbuch auf LittleFS
    ├── system.cpp  / system.h  # System-Health, TPL5110-Watchdog
    ├── statusled.cpp           # Leer-Stubs (WS2812 auf N16R8 deaktiviert)
    ├── icons.h                 # PWA-Icons als PROGMEM-Arrays
    └── web/
        ├── web.h               # Webserver-Header, Routing
        ├── web_root.cpp        # Startseite (Dashboard)
        ├── web_pages.cpp       # RGB-Seite, Systemtest, Logbuch, OTA
        ├── web_settings.cpp    # Einstellungs- und MQTT-Seite
        └── web_helpers.cpp     # HTML-Bausteine, CSS, gemeinsame Elemente
```

---

## 🔬 Lux-Verarbeitungs-Pipeline

```
VEML7700 Hardware
      │
      ▼
┌─────────────┐
│ Rohwert     │  getLux() – direkte I²C-Abfrage
│ Messung     │  Fehler → NAN, Wire.setTimeOut(15ms)
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ Median-     │  medianLux(rawValue)
│ Filter      │  Puffergröße 5, sortierte Auswahl des Mittenwerts
│             │  Filtert: Spitzen, Einzelausreißer
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ EMA-Filter  │  lux = lux * 0.8 + raw * 0.2
│ (exponential│  Glättet: kurze Schwankungen, Rauschen
│ moving avg) │
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ Trend-      │  updateLuxTrend() alle 30 s
│ Berechnung  │  luxRate = (lux - lastLux) / deltaTime
│             │  → lx/min Änderungsrate
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ Prognose    │  runAutomatik()
│ Logik       │  minutesToThresh = (lux - threshold) / luxRate
│             │  Entscheidung: Vorlicht starten / Schließen
└─────────────┘
```

---

## 🔄 Zustandsmaschinen

### Türzustand (DoorPhase)

```
    ─────────────                          ──────────────
    │   IDLE    │ ──── Öffnen ausgelöst ──► │  OPENING   │
    │           │                           │            │
    └─────┬─────┘                           └──────┬─────┘
          ▲                                        │ Endposition / Timeout
          │                                 ───────▼──────
    Schließen                               │    OPEN    │
    abgeschlossen                           │            │
          │                                 └──────┬─────┘
    ──────┴──────                                  │ Schließen ausgelöst
    │  CLOSING  │ ◄──────────────────────────────── ┘
    │           │
    └───────────┘
```

### Lichtzustand (LightState)

```
LIGHT_OFF
   │
   ├──► LIGHT_PRE_OPEN    (Vorlicht vor Türöffnung)
   │         │
   │         ▼
   │    LIGHT_POST_OPEN   (Nachlicht nach Öffnung)
   │         │
   │         ▼
   │    LIGHT_OFF
   │
   └──► LIGHT_PRE_CLOSE   (Vorlicht vor Türschließung)
             │              Wolken-Abort möglich
             │              Fenster-Check entfernt (Winter-Fix)
             ▼
        LIGHT_POST_CLOSE  (Nachlicht nach Schließung, mit Dimmen)
             │
             ▼
        LIGHT_OFF
```

---

## 🤖 Automatik-Logik

Die Funktion `runAutomatik()` in `logic.cpp` wird alle 50 ms aufgerufen:

```
runAutomatik()
│
├── Tages-Reset um 03:00 Uhr
│     └── lastOpenActionMin = -1  ← verhindert Off-by-one-day Bug
│         lastCloseActionMin = -1
│
├── Modus = "time"?
│     └── Zeitfenster ±1 Minute → Öffnen / Schließen
│
├── Modus = "lux"?
│     ├── luxReady && luxValid?
│     │     ├── lux > openThreshold → Tür öffnen
│     │     ├── lux < closeThreshold
│     │     │     ├── Prognose aktiv? → minutesToThresh berechnen
│     │     │     ├── Vorlicht starten (PRE_CLOSE)
│     │     │     └── lux stabil < Schwelle → Schließen auslösen
│     │     └── Wolken-Abort: lux steigt → PRE_CLOSE abbrechen
│     │
│     └── luxReady = false → Fallback auf Zeitsteuerung
│
├── Nacht-Sperre aktiv? → kein Öffnen
├── manualOverrideUntil > jetzt? → Automatik pausiert
└── actionLock? → warten bis Motor stoppt
```

**Bekannte Fixes:**
- `lastOpenActionMin` täglich um 03:00 auf `-1` zurückgesetzt (Off-by-one-day Bug)
- Zeitfenster ±1 Minute für Öffnung/Schließung (Loop-Blockade-Schutz)
- `inCloseWindow`-Check in PRE_CLOSE HOLD entfernt (Winter-Sonnenuntergänge)

---

## ⏱ Loop-Architektur

Der Haupt-Loop in `main.cpp` läuft alle **50 ms** (`LOGIC_INTERVAL`):

```
loop()
│
├── wdogFeed()                   ← Software + TPL5110 Hardware-Watchdog
├── server.handleClient()        ← WebServer, immer
│
├── if (otaInProgress) return    ← OTA hat Vorrang
│
├── mqttLoop()                   ← MQTT keep-alive + empfangen
├── wifiWatchdog()               ← Reconnect + server.begin() bei Reconnect
│
├── updateMotor()                ← Timeout + Endschalter prüfen
├── updateButton()               ← Taster Tür
├── updateStallButton()          ← Taster Stalllicht
├── updateRedButton()            ← Taster Rotlicht
│
├── getLux() (alle 1 s)          ← Sensor lesen
├── medianLux()                  ← Medianfilter
├── EMA-Filter                   ← Exponentielles Glätten
├── checkLuxHealth()             ← Fehlerüberwachung + Recovery
├── updateLuxTrend() (alle 30 s) ← Änderungsrate berechnen
│
├── updateSystemHealth()         ← Heap, Uptime, Sensor-Status
├── updateDimming()              ← RGB-Dimmer aktualisieren
├── updateStallLightTimer()      ← Stalllicht-Timer
│
├── runAutomatik()               ← Automatik-Logik
└── updateLightState()           ← Lichtzustandsmaschine
```

---

## 🛡 Hardware-Watchdog (TPL5110)

`wdogFeed()` in `system.cpp` wird bei jedem Loop-Durchlauf aufgerufen:

- **Software-Watchdog:** Wenn Loop > 60 s nicht aufgerufen → ESP.restart()
- **TPL5110-Puls:** Alle 20 s ein HIGH-Puls auf GPIO 6
- **Beim Boot:** Puls an mehreren Stellen in `setup()` → kein Reset beim langen WiFi-Connect
- **Während WiFi-Warten:** Pulse alle 5 s in der Wartezeit

Der TPL5110 resettet den ESP unabhängig von der Software über den EN-Pin – auch bei komplettem Loop-Freeze.
