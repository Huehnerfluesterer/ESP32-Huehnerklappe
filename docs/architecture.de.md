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
- [Watchdog](#-watchdog-esp32-task-wdt)
- [ESP-NOW](#-esp-now-dispatcher)
- [Persistenz](#-persistenz-eeprom--littlefs)

---

## 🗺 Überblick

```
┌────────────────────────────────────────────────────────────┐
│                          main.cpp                          │
│       Setup · Loop · WebServer-Routen · OTA-Update         │
└────┬────────────────────────────────────────────────┬──────┘
     │                                                │
┌────▼─────┐  ┌──────────┐  ┌──────────┐  ┌───────────▼──────┐
│  logic   │  │   lux    │  │  motor   │  │      web/        │
│ Automatik│  │ VEML7700 │  │  L298N   │  │  Webserver + PWA │
└────┬─────┘  └──────────┘  └──────────┘  └──────────────────┘
     │
┌────▼─────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────┐
│  light   │  │   door   │  │   mqtt   │  │     storage      │
│ RGB/PWM  │  │  Taster  │  │  Client  │  │  EEPROM/LittleFS │
└──────────┘  └──────────┘  └──────────┘  └──────────────────┘
     │
┌────▼─────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────┐
│  system  │  │  logger  │  │  wlan    │  │    telegram      │
│ Health   │  │ Logbuch  │  │ Watchdog │  │ Push-Nachrichten │
│ Task-WDT │  │          │  │          │  │                  │
└──────────┘  └──────────┘  └──────────┘  └──────────────────┘
     │
┌────▼───────────────┐  ┌───────────────────┐
│     bme.cpp        │  │    relay.cpp      │
│ BME280 lokal/ESPNOW│  │ Remote-Relais ESP │
└────────────────────┘  └───────────────────┘
     ▲                         ▲
     │                         │
     └──────── espnow_dispatch.cpp (zentraler recv_cb) ────┘
```

---

## 📁 Projektstruktur

```
ESP-Huehnerklappe/
├── platformio.ini              # Build-Konfiguration
├── partitions_16mb_ota.csv     # Partitionstabelle 16 MB Flash
├── boards/
│   ├── esp32-s3-devkitc-1-n16r8.json
│   └── esp32-s3-devkitc-1-n16r2.json
├── docs/                       # Diese Dokumentation
├── ESP-NOW/                    # Companion-Sketches
│   ├── bme280_sender/          # Außensensor-ESP32
│   └── relay_esp/              # Relais-Node (ESP32-C3)
└── src/
    ├── config.h                # ⚠️ WLAN-Zugangsdaten (in .gitignore!)
    ├── pins.h                  # Alle GPIO-Pin-Definitionen
    ├── types.h                 # Enums + Settings/MqttSettings/TelegramSettings
    ├── main.cpp                # Setup, Loop, alle WebServer-Routen, OTA
    ├── motor.cpp / motor.h     # L298N-Ansteuerung via LEDC-PWM
    ├── door.cpp  / door.h      # Türzustand, Taster-Handling
    ├── lux.cpp   / lux.h       # VEML7700: Median + EMA + Trend + Recovery
    ├── light.cpp / light.h     # Licht-Automatik, RGB-PWM, Dimmer
    ├── logic.cpp / logic.h     # Automatik-Entscheidungslogik
    ├── mqtt.cpp  / mqtt.h      # PubSubClient-Wrapper, Topics, Publish
    ├── telegram.cpp/ telegram.h# Telegram-Events + Deadline-Alarm
    ├── bme.cpp   / bme.h       # BME280 lokal oder ESP-NOW
    ├── relay.cpp / relay.h     # Remote-Relais via ESP-NOW (Sender)
    ├── espnow_dispatch.cpp/.h  # Zentraler ESP-NOW recv_cb
    ├── wlan.cpp  / wlan.h      # WLAN-Verbindung, Watchdog, Reconnect
    ├── storage.cpp / storage.h # EEPROM Lesen/Schreiben + Persistenz
    ├── logger.cpp  / logger.h  # Ringpuffer-Logbuch + LittleFS deferred write
    ├── system.cpp  / system.h  # Task-WDT, OTA Safe State, DS3231, Sim-Offset
    ├── statusled.cpp           # Leer-Stubs (WS2812 auf N16R8 deaktiviert)
    ├── icons.h                 # PWA-Icons als PROGMEM-Arrays
    └── web/
        ├── web.h               # Webserver-Header, Routing
        ├── web_root.cpp        # Startseite (Dashboard) + Status-JSON
        ├── web_pages.cpp       # Erweitert, ESP-NOW, Blockade, RGB,
        │                       #  MQTT, Telegram, Logbuch, Simulation, OTA
        ├── web_settings.cpp    # Betriebseinstellungen
        └── web_helpers.cpp     # HTML-Bausteine, CSS, gemeinsame Elemente
```

---

## 🔬 Lux-Verarbeitungs-Pipeline

Die Helligkeit wird im Loop einmal pro Sekunde gelesen und durch eine
mehrstufige Pipeline geschickt, bevor sie in die Automatik‑Entscheidung
eingeht:

```
VEML7700 Hardware
      │
      ▼
┌──────────────┐
│ Rohwert      │  getLux() – direkte I²C-Abfrage
│ Messung      │  Fehler → NAN, Wire.setTimeOut(20 ms)
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Median-      │  medianLux(rawValue)
│ Filter       │  Puffergröße 5, Mittenwert
│              │  Filtert: Spitzen, Einzelausreißer
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ EMA-Filter   │  lux = lux * 0.8 + raw * 0.2
│ (exponential │  Glättet: kurze Schwankungen, Rauschen
│ moving avg)  │
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Trend-       │  updateLuxTrend() alle 30 s
│ Berechnung   │  luxRate = (lux - lastLux) / deltaTime
│              │  → lx / min Änderungsrate
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Gesundheits- │  checkLuxHealth()
│ Check        │  Hard-Fail → lichtAutomationAvailable = false
│              │  + Telegram-Alarm + Auto-Reboot 03:00
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Prognose     │  runAutomatik()
│ Logik        │  minutesToThresh = (lux − threshold) / luxRate
│              │  → Vorlicht starten / Schließen anstoßen
└──────────────┘
```

---

## 🔄 Zustandsmaschinen

### Türzustand (`DoorPhase`)

```
    ─────────────                          ──────────────
    │   IDLE    │ ──── Öffnen ausgelöst ──►│  OPENING   │
    │           │                          │            │
    └─────┬─────┘                          └──────┬─────┘
          ▲                                       │ Endposition / Timeout
          │                                ───────▼──────
    Schließen                              │    OPEN    │
    abgeschlossen                          │            │
          │                                └──────┬─────┘
    ──────┴──────                                 │ Schließen ausgelöst
    │  CLOSING  │ ◄────────────────────────────── ┘
    │           │
    └───────────┘
```

### Lichtzustand (`LightState`)

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
   └──► LIGHT_PRE_CLOSE   (Vorlicht vor Türschließung, Keep-Alive)
             │              Wolken-Abort möglich
             ▼
        LIGHT_POST_CLOSE  (Nachlicht nach Schließung, sanft gedimmt)
             │
             ▼
        LIGHT_OFF
```

---

## 🤖 Automatik-Logik

`runAutomatik()` in `logic.cpp` wird alle 50 ms aufgerufen:

```
runAutomatik()
│
├── Tages-Reset um 03:00 Uhr
│     └── lastOpenActionMin = -1
│         lastCloseActionMin = -1
│
├── Modus = "time"?
│     └── Zeitfenster ±1 Minute → Öffnen / Schließen
│
├── Modus = "light"?
│     ├── luxReady && luxValid?
│     │     ├── lux > openThreshold   → Tür öffnen
│     │     ├── lux < closeThreshold
│     │     │     ├── Prognose aktiv? → minutesToThresh berechnen
│     │     │     ├── Vorlicht starten (PRE_CLOSE)
│     │     │     └── lux stabil < Schwelle → Schließen auslösen
│     │     └── Wolken-Abort: lux steigt wieder → PRE_CLOSE abbrechen
│     │
│     └── Lichtsensor defekt → Fallback auf Zeitsteuerung
│          (motorReason = "Zeit-Fallback")
│
├── Nacht-Sperre aktiv? → kein Öffnen
├── manualOverrideUntil > jetzt? → Automatik pausiert
└── actionLock? → warten bis Motor stoppt
```

**Bekannte Fixes (im Code hinterlegt):**

- `lastOpenActionMin` täglich um 03:00 auf `-1` zurückgesetzt
  (Off‑by‑one‑day Bug)
- Zeitfenster ±1 Minute für Öffnung/Schließung (Loop‑Blockade‑Schutz)
- `inCloseWindow`‑Check in PRE_CLOSE HOLD entfernt (Winter‑Sonnenuntergänge)
- Bei VEML‑Hardfehler automatischer Fallback auf `time` und geplanter
  Neustart um 03:00

---

## ⏱ Loop-Architektur

Der Haupt‑Loop in `main.cpp` läuft alle **50 ms**
(`LOGIC_INTERVAL = 50`) und ruft folgende Funktionen in fester
Reihenfolge auf:

```
loop()
│
├── wdogFeed()                   ← Task-WDT füttern
├── server.handleClient()        ← WebServer, IMMER
│
├── if (otaInProgress) return    ← OTA hat Vorrang
│
├── mqttLoop()                   ← MQTT keep-alive + Callback
├── wifiWatchdog()               ← Reconnect-Logik
├── NTP-Nachsync (alle 10 s, falls noch nicht synchron)
│
├── updateMotor()                ← Timeout + Endschalter prüfen
├── updateButton()               ← Taster Tür
├── updateStallButton()          ← Taster Stalllicht
├── updateRedButton()            ← Taster Rotlicht
│
├── getLux() (alle 1 s)          ← Sensor lesen
├── medianLux()                  ← Median-Filter
├── EMA-Filter                   ← Exponentielles Glätten
├── checkLuxHealth()             ← Fehlerüberwachung + Recovery
├── updateLuxTrend() (alle 30 s) ← Änderungsrate berechnen
│
├── updateSystemHealth()         ← WiFi/MQTT/Sensor Flags
├── bmeUpdate()                  ← BME280 lokal oder ESP-NOW Timeout
├── relaySync()                  ← 60-s Resync des Remote-Relais
├── loggerUpdate()               ← LittleFS deferred write (30 s)
│
├── updateDimming()              ← RGB-Dimmer Frame
├── updateStallLightTimer()      ← Stalllicht-Timer
│
├── VEML-Hard-Fail-Neustart 03:00 (einmalig pro Tag)
├── Tages-Statistik 23:55 per MQTT (tele/daily)
│
├── runAutomatik()               ← Automatik-Logik
├── telegramDeadlineCheck()      ← Alarm bei versäumter Öffnung
├── updateLightState()           ← Licht-Zustandsmaschine
└── statusLedUpdate()            ← (Stub)
```

---

## 🛡 Watchdog (ESP32 Task-WDT)

`wdogFeed()` in `system.cpp` ruft `esp_task_wdt_reset()` auf und wird
einmal pro Loop‑Durchlauf (also alle ≥ 50 ms im Normalbetrieb)
aufgerufen.

```cpp
#define WDOG_TIMEOUT_MS  30000UL   // 30 s ohne Feed → Reset
```

Der Watchdog wird in `tpl5110Init()` scharfgeschaltet – der
Funktionsname stammt noch aus der Zeit, als hier ein externer TPL5110
an GPIO 6 initialisiert wurde. Heute konfiguriert die Funktion nur
noch den **internen** `esp_task_wdt`:

```cpp
esp_task_wdt_config_t cfg = {
    .timeout_ms     = WDOG_TIMEOUT_MS,
    .idle_core_mask = 0,
    .trigger_panic  = true,
};
esp_task_wdt_reconfigure(&cfg);
esp_task_wdt_add(NULL);
```

Bei einem Watchdog‑Reset ist die Reset‑Reason `ESP_RST_TASK_WDT`. Die
Firmware prüft diesen Wert in `telegramInit()` und sendet – sofern
Telegram aktiv ist – direkt nach dem Reboot eine Nachricht:

> 🔄 Neustart durch Watchdog!
> Die Steuerung hat sich aufgehängt und wurde automatisch neu gestartet.

Während **OTA** werden Motor, Licht und Stalllicht in einen sicheren
Zustand versetzt (`enterIoSafeState()` in `system.cpp`) und der Loop
führt nur noch `server.handleClient()` aus.

---

## 📡 ESP-NOW Dispatcher

ESP‑NOW wird für zwei unabhängige Funktionen genutzt:

1. **Außensensor** – zweiter ESP32 mit BME280, sendet alle 10 s per
   Broadcast an die Klappe (`bme.cpp` empfängt).
2. **Remote‑Relais** – ESP32‑C3‑Node, der bei Automatik‑Öffnung AN
   und bei Automatik‑Schließung AUS geschaltet wird und alle 30 s
   einen Heartbeat zurückschickt (`relay.cpp` sendet, empfängt den
   Heartbeat).

Beide Module würden normalerweise einen eigenen `esp_now_register_recv_cb`
benötigen – ESP‑NOW erlaubt aber nur **einen** einzigen Receive‑Callback
pro ESP. Deshalb gibt es `espnow_dispatch.cpp`:

```cpp
void espnowDispatcherInit() {
    esp_now_register_recv_cb(onEspNowReceive);
}

static void onEspNowReceive(const uint8_t *mac, const uint8_t *data, int len) {
    uint32_t magic;
    memcpy(&magic, data, sizeof(magic));
    if (magic == BME_PACKET_MAGIC)        { /* → bme   */ }
    else if (magic == RELAY_PACKET_MAGIC) { /* → relay */ }
}
```

Jedes Paket trägt einen eindeutigen 32‑Bit‑Magic‑Prefix, sodass der
Dispatcher anhand des ersten Feldes entscheiden kann, an welches Modul
das Paket geht. Details siehe [espnow.de.md](espnow.de.md).

---

## 💾 Persistenz (EEPROM + LittleFS)

Alle Einstellungen werden in einer **1024 Byte großen EEPROM‑Region**
gespeichert. Die wichtigsten Adressen (aus `storage.h`):

| Adresse | Inhalt |
|---|---|
| `0`   | `Settings` (Modi, Zeiten, Lux, Lichtdauer) |
| `100` | `openPosition` (long, Motorlaufzeit Öffnen in ms) |
| `104` | `closePosition` (long, Motorlaufzeit Schließen in ms) |
| `120` | `useLimitSwitches` (bool) |
| `200` | `MqttSettings` (Host, Port, User, Pass, ClientId, Base‑Topic) |
| `400` | `doorOpen` (bool, letzter bekannter Türzustand) |
| `450` | `theme` (char[10], Dark/Light Mode) |
| `460` | `blockadeEnabled` (bool) + `blockadeThresholdA` (float) |
| `470` | `bmeSource` (0 = lokal, 1 = ESP‑NOW) |
| `480` | `relayEnabled` (bool) + `relayMac[6]` (uint8_t) |
| `490` | RGB‑Farbe: `r, g, b, w, brightness` (5 Bytes) |
| `496` | `closeDelayMin` (uint8_t, 0–30 min) |
| `600` | `TelegramSettings` (Token, Chat‑ID, Deadline, Notify‑Flags) |
| `1023`| Magic‑Byte (`0xA7`) – markiert initialisiertes EEPROM |

Das **Logbuch** liegt zusätzlich als Ringpuffer auf **LittleFS**
(`/log.txt`). Neue Einträge werden im RAM gehalten und alle 30 s von
`loggerUpdate()` ins Dateisystem geschrieben (deferred write).
Download und Löschen sind über `/log/download` bzw. `/log/clear`
möglich.
