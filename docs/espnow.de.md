# 📡 ESP-NOW

🇩🇪 Deutsch · Firmware `3.0.16`

Die Hühnerklappe unterstützt zwei ESP-NOW-Teilprojekte:

1. **BME280-Außensensor** – separater ESP32 sendet Temperatur, Luft­feuchte
   und Luftdruck per Broadcast an die Klappe (für Außen­temperatur-Anzeige
   und Entscheidungen).
2. **Relais-Node** – ein ESP32-C3 schaltet ein externes Relais. Die Klappe
   sendet ausschließlich Automatik-Events (Öffnung/Schließung) an den Node;
   manuelle Bedienung per Taster oder WebUI bleibt davon unberührt.

Beide Teilprojekte teilen sich **einen einzigen** Receive-Callback auf der
Klappe – siehe `src/espnow_dispatch.cpp`.

---

## Inhaltsverzeichnis

- [Grundprinzip & Kanal-Sync](#-grundprinzip--kanal-sync)
- [Paketformate](#-paketformate)
- [BME280-Außensensor](#-bme280-außensensor)
- [Relais-Node (ESP32-C3)](#-relais-node-esp32-c3)
- [Fehlerdiagnose](#-fehlerdiagnose)

---

## 📶 Grundprinzip & Kanal-Sync

ESP-NOW läuft auf einem festen WLAN-Kanal und funktioniert nur, wenn
Sender und Empfänger denselben Kanal verwenden. Da der Router den Kanal
bestimmt, sind **alle beteiligten ESPs im selben WLAN eingebucht** –
nicht für die eigentliche IP-Kommunikation, sondern nur damit alle auf
denselben Kanal fallen.

Die Klappe initialisiert ESP-NOW daher **nach** dem WLAN-Connect:

```cpp
// src/main.cpp (setup)
WiFi.mode(WIFI_STA);
WiFi.begin(...);
while (WiFi.status() != WL_CONNECTED && millis()-t0 < 15000) delay(50);

bmeInit();               // ESP-NOW init nach WLAN – Kanal stimmt jetzt
relayInit();             // Relais-Peer auf aktuellem Kanal
espnowDispatcherInit();  // einziger recv_cb – leitet an bme/relay weiter
```

Der zentrale Dispatcher unterscheidet die Pakete über einen 4-Byte-
Magic-Header und leitet sie an das richtige Modul weiter:

```
         onEspNowReceive(mac, data, len)
                      │
           ┌──────────┴──────────┐
           ▼                     ▼
   magic == 0xBEEF280      magic == 0xC10534
   (BME-Paket)             (Relay-Paket)
           │                     │
           ▼                     ▼
   bmeTemp/Hum/Pres setzen  relayLastHeartbeat = millis()
   bmeLastReceived = ms     (CMD_HEARTBEAT)
```

---

## 📦 Paketformate

Die Struct-Definitionen müssen in **Sender und Empfänger** bitgleich sein –
Reihenfolge, Typen und Padding zählen. Als Schutz vor Fremdpaketen liegt
in jedem Paket ein 4-Byte-Magic-Header als erstes Feld.

### BME280-Paket (`src/bme.h`)

```cpp
struct BmePacket {
    uint32_t magic;      // 0xBEEF280
    float    temp;       // °C
    float    humidity;   // %
    float    pressure;   // hPa
};
```

Plausibilitätsprüfung im Dispatcher:

| Feld | Erlaubter Bereich |
|---|---|
| `temp` | −40 … +85 °C |
| `humidity` | 0 … 100 % |
| `pressure` | 300 … 1100 hPa |

Ungültige Pakete werden stillschweigend verworfen. Timeout:
`bmeOk = false` wenn länger als 60 s kein Paket.

### Relais-Paket (`src/relay.h`)

```cpp
struct RelayPacket {
    uint32_t magic;      // 0xC10534
    uint8_t  command;    // 0 = AUS, 1 = AN, 2 = HEARTBEAT
};
```

| Konstante | Wert |
|---|---|
| `RELAY_PACKET_MAGIC` | `0xC10534` |
| `RELAY_CMD_OFF` | `0` |
| `RELAY_CMD_ON` | `1` |
| `RELAY_CMD_HEARTBEAT` | `2` |

---

## 🌡 BME280-Außensensor

**Ordner:** `ESP-NOW/bme280_sender/`
**Zielboard:** beliebiger ESP32 mit BME280 am I²C-Bus
**Sendeintervall:** alle 10 s (`SEND_INTERVAL_MS`)

### Verkabelung

```
BME280 VCC ──── 3,3 V   (nicht 5 V!)
BME280 GND ──── GND
BME280 SDA ──── GPIO 21
BME280 SCL ──── GPIO 22
```

I²C-Adresse: `0x76` (SDO auf GND) oder `0x77` (SDO auf VCC) – im Sketch
`#define BME_ADDRESS 0x76` anpassen.

### Konfiguration (Sender-Sketch)

```cpp
// ESP-NOW/bme280_sender/src/main.cpp
#define WIFI_SSID      "DeinWLAN"
#define WIFI_PASSWORD  "DeinWLANPasswort"
#define SEND_INTERVAL_MS  10000UL
#define BME_ADDRESS       0x76
```

**Keine Ziel-MAC nötig!** Der Sender sendet an die Broadcast-MAC
`FF:FF:FF:FF:FF:FF`, die Klappe nimmt jedes passende Paket an. Das macht
den Austausch eines Senders unkompliziert.

### Flashen & In-Betrieb-nehmen

```bash
cd ESP-NOW/bme280_sender
pio run -t upload
pio device monitor
```

Erwartete Ausgabe:

```
🌡 BME280 ESP-NOW Sender
========================
✅ BME280 gefunden (0x76)
📡 Verbinde mit DeinWLAN ...
✅ WLAN verbunden – Kanal 6
📡 Eigene MAC: 24:6F:28:XX:XX:XX
✅ Broadcast-Peer registriert
Sende alle 10 s

[Messung] 19.3°C  62.1%  1013.4 hPa
[ESP-NOW] Paket #1 gesendet ✓
```

### Empfangsseite (Klappe)

Auf der Klappe unter **`/advanced`** → **BME280-Quelle** auf `ESP-NOW`
setzen (`POST /save-bme-source`) und neu starten. Danach zeigt
`/espnow` die zuletzt empfangene Sender-MAC und das Alter des letzten
Pakets an.

Wird im `BME_SOURCE_LOCAL`-Modus ein Paket empfangen, wird es **ignoriert**
– die Umschaltung erfolgt ausschließlich über die Einstellung, nicht
automatisch.

---

## 🔌 Relais-Node (ESP32-C3)

**Ordner:** `ESP-NOW/relay_esp/`
**Zielboard:** ESP32-C3-DevKitM-1 (PlatformIO-Env `esp32dev`,
              Board-Typ ESP32C3 Dev Module)
**Features:** ESP-NOW-Empfänger, eigenes WebUI, optionales MQTT,
              30-Sekunden-Heartbeat zurück zur Klappe

### Wofür ist der Relais-Node gedacht?

Szenarien, bei denen ein Relais **örtlich getrennt** von der Klappe
gebraucht wird – z. B. ein Außenlicht am Gartentor, das bei Auf­/Zu
automatisch mitgeschaltet werden soll, aber keinen Kabel­zug zur Klappe
vertragen würde. Die Klappe „fernsteuert" den Node per ESP-NOW.

### Trigger-Verhalten (aus `src/relay.h`)

Der Node wird **nur bei Automatik-Events** geschaltet:

- Lichtautomatik öffnet  → `relaySendOn()`
- Zeitautomatik öffnet   → `relaySendOn()`
- Lichtautomatik schließt → `relaySendOff()`
- Zeitautomatik schließt  → `relaySendOff()`

**Manuelle Bedienung** (Taster, WebUI, MQTT `cmnd/door`) schaltet den
Relais-Node bewusst **nicht** mit. Wer das ändern will, hängt den
entsprechenden `relaySend...()`-Aufruf in den Handler.

### Anti-Drift: Sync alle 60 s

Der Node könnte einen Befehl verpassen (Paketverlust, Reboot, ausser Reichweite).
Damit er nicht dauerhaft in einem falschen Zustand steht, läuft im Loop
der Klappe `relaySync()`:

- Nur wenn `relayExpectedValid` (Automatik hat mindestens einmal geschaltet)
- Nur wenn Heartbeat vom Node **jünger als 90 s** ist (sonst ist er ohnehin offline)
- Sendet alle 60 s erneut den erwarteten Zustand
- Loggt `🔁 Relais-Sync: AN` bzw. `AUS`

### Konfiguration (Node-Sketch)

```cpp
// ESP-NOW/relay_esp/src/main.cpp
#define WIFI_SSID        "DeinWLAN"
#define WIFI_PASSWORD    "DeinWLANPasswort"

#define RELAY_PIN         10
#define RELAY_ACTIVE_LOW  true     // an eigene Hardware anpassen!

#define MQTT_TOPIC_STATE  "huehner/relais/state"
#define MQTT_TOPIC_CMD    "huehner/relais/cmd"
#define HEARTBEAT_MS      30000UL
```

### Flashen & MAC ablesen

```bash
cd ESP-NOW/relay_esp
pio run -t upload
pio device monitor
```

Im Monitor die **MAC-Adresse** des ESP32-C3 ablesen (wird beim Boot
ausgegeben).

### Verknüpfung mit der Klappe

1. Klappe öffnen → **`/advanced`** → **Relais-MAC eintragen** (z. B.
   `24:6F:28:AA:BB:CC`) → Speichern (`POST /save-relay`).
2. Checkbox **Relais-ESP aktivieren** setzen.
3. Klappe oder Node neu starten.
4. Auf `/espnow` den Heartbeat-Eintrag beobachten – er sollte alle 30 s
   „älter als X s" auf 0 zurückspringen.
5. Ein manuelles Automatik-Event auslösen (z. B. per Simulation),
   Node-LED oder MQTT-State kontrollieren.

### Node-WebUI & MQTT

Der Node hat ein eigenes Mini-WebUI auf Port 80 (IP aus Router/Monitor).
Dort lässt sich das Relais **manuell** schalten, auch unabhängig von der
Klappe – und optional MQTT konfigurieren. Topics (fest verdrahtet im
Sketch, siehe Konstanten oben):

- `huehner/relais/state` – Status `ON`/`OFF` (retained)
- `huehner/relais/cmd` – Befehl `ON`/`OFF`/`TOGGLE`

---

## 🔍 Fehlerdiagnose

### Keine BME-Werte auf der Klappe

1. `/advanced` prüfen: Quelle tatsächlich auf `ESP-NOW`?
2. Serieller Monitor des Senders: kommen Pakete raus?
3. **Kanal-Sync:** Beide ESPs müssen im selben WLAN sein. Wenn der Router
   auf einem anderen Kanal sitzt als bei der letzten Sender-Session,
   einmal den Sender neu starten.
4. Adresse am Sender (`BME_ADDRESS`) passt zum BME-Modul?
5. Plausibilitätsprüfung: Werte außerhalb der Bereiche werden verworfen –
   kalter BME bei −45 °C wird stillschweigend ignoriert.

### Relais-Node antwortet nicht

1. MAC im WebUI der Klappe **exakt richtig**? (Großschreibung ist egal,
   Doppelpunkte werden akzeptiert.)
2. Checkbox **Relais-ESP aktivieren** gesetzt? Sonst ruft `sendCommand()`
   gar nicht auf.
3. Heartbeat-Alter auf `/espnow` < 90 s? Sonst unterdrückt `relaySync()`
   den periodischen Resync, damit keine sinnlosen Broadcasts rausgehen.
4. Reset am Relais-Node – Boot-Log prüfen, ob Peer-Registrierung funktionierte.

### Widersprüchliche MAC-Meldungen

Der Relais-Peer wird beim MAC-Wechsel per `relayReset()` entfernt und neu
registriert. Falls nötig, auf der Klappen-Seite **/advanced → MAC
speichern** erneut drücken – der Handler ruft `relayReset()` automatisch auf.

### ESP-NOW verlangsamt MQTT / Webserver

Das dürfte nicht passieren – `esp_now_send()` ist nicht-blockierend, und
der Dispatcher macht im Callback nur memcpy + Plausibilitätsprüfung.
Wenn doch etwas hängt: Task-WDT-Timeout steht bei 30 s und beißt nach
einem Freeze zuverlässig zu.
