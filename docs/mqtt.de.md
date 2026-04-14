# 📡 MQTT

🇩🇪 Deutsch

Die Hühnerklappe sendet Telemetrie und akzeptiert Befehle über MQTT.
Alle Topics liegen unterhalb eines konfigurierbaren **Base‑Topics**
(Standard: `huehnerklappe`).

---

## Inhaltsverzeichnis

- [Konfiguration](#konfiguration)
- [Topic-Struktur](#topic-struktur)
- [Telemetrie (`tele/`)](#telemetrie-tele)
- [Befehle (`cmnd/`)](#befehle-cmnd)
- [Last Will & Testament](#last-will--testament)
- [Beispiele](#beispiele)

---

## Konfiguration

MQTT wird unter **Erweitert → 📡 MQTT Einstellungen** (`/mqtt`) im
Web‑Interface konfiguriert:

| Feld | Beschreibung | Beispiel |
|---|---|---|
| Host | IP oder Hostname des Brokers | `192.168.1.100` |
| Port | Standard MQTT‑Port | `1883` |
| Client‑ID | Eindeutige ID | `huehnerklappe` |
| Base‑Topic | Präfix für alle Topics | `huehnerklappe` |
| Benutzer | Optional | |
| Passwort | Optional | |

Die Werte werden im EEPROM (ab Adresse `200`) persistiert. Ein
Verbindungstest ist über den Button **Test** (`/mqtt-test`) möglich.

---

## Topic-Struktur

```
<base>/tele/...    ← von der Klappe gesendet (Telemetrie)
<base>/cmnd/...    ← an die Klappe gesendet  (Befehle)
```

Im Folgenden wird `<base>` mit dem konfigurierten Base‑Topic ersetzt
(z. B. `huehnerklappe`).

---

## Telemetrie (`tele/`)

Die Klappe publiziert **nicht retained**, mit Ausnahme der
Availability und der Settings.

| Topic | Intervall / Trigger | Retained | Inhalt |
|---|---|:---:|---|
| `<base>/tele/availability` | Connect / LWT | ✅ | `online` / `offline` |
| `<base>/tele/status` | alle 10 s + Event | ❌ | JSON, kompletter Systemstatus |
| `<base>/tele/settings` | bei Verbindung + Änderung | ✅ | JSON, aktuelle Betriebseinstellungen |
| `<base>/tele/log` | bei jedem Log‑Eintrag | ❌ | Plain‑Text, eine Zeile |
| `<base>/tele/daily` | täglich 23:55 | ❌ | JSON, Tagesstatistik |

### Status-JSON (`tele/status`)

Die Klappe publiziert bei jeder Zustandsänderung sowie alle 10 s ein
Status‑Objekt:

```json
{
  "time": "18:42:31",
  "date": "14.4.2026",
  "door": "Offen",
  "moving": "0",
  "door_pct": 100,
  "light": "1250.4",
  "sensor": "VEML7700 OK",
  "lightState": "Aus",
  "stallLight": "Aus",
  "fw": "3.0.16",
  "statOpen": 1,
  "statClose": 0,
  "statMinOpen": 423
}
```

| Feld | Bedeutung |
|---|---|
| `time` / `date` | Systemzeit (RTC oder NTP) |
| `door` | `Offen` oder `Geschlossen` |
| `moving` | `"1"` wenn der Motor gerade fährt, sonst `"0"` |
| `door_pct` | Türposition in Prozent (0 = zu, 100 = auf) |
| `light` | Aktueller Lux‑Wert als String, `n/a` wenn ungültig |
| `sensor` | Sensor‑Status (`VEML7700 OK` / `Lichtsensor Fehler`) |
| `lightState` | Locklicht AN/AUS |
| `stallLight` | Stalllicht AN/AUS |
| `fw` | Firmware‑Version |
| `statOpen` / `statClose` | Zähler für heute |
| `statMinOpen` | Minuten, die die Klappe heute offen war |

### Settings-JSON (`tele/settings`, retained)

```json
{
  "openMode": "light",
  "closeMode": "light",
  "openTime": "06:30",
  "closeTime": "21:45",
  "openLightThreshold": 300,
  "closeLightThreshold": 15,
  "lampPreOpen": 5,
  "lampPostOpen": 5,
  "lampPreClose": 10,
  "lampPostClose": 15
}
```

Der Modus kennt intern nur die Werte `"light"` und `"time"`. Manuelle
Steuerung (Taster / Web / MQTT) pausiert die Automatik über
`manualOverrideUntil` – der Modus‑String ändert sich dabei **nicht**.

### Tagesstatistik (`tele/daily`)

Wird täglich um 23:55 einmal publiziert und danach intern
zurückgesetzt:

```json
{
  "date": "14.4.2026",
  "opens": 1,
  "closes": 1,
  "open_min": 842,
  "sensor_errors": 0
}
```

### Logbuch (`tele/log`)

Reiner Plain‑Text‑Topic, **eine Zeile pro Log‑Eintrag**. Beispiele:

```
Öffnung gestartet (MQTT) – Automatik pausiert 15 min
Schließvorgang gestartet (Lichtautomatik, 8.2 lx)
📡 Relais AN gesendet
```

Das gesamte Logbuch kann per Befehl (`cmnd/get LOG`) zusätzlich
abgefragt werden.

---

## Befehle (`cmnd/`)

Die Klappe abonniert `<base>/cmnd/#` und reagiert auf folgende Topics.
Payloads sind **case‑insensitive** (`open` / `OPEN` / `Open` ist
identisch).

| Topic | Payload | Aktion |
|---|---|---|
| `<base>/cmnd/door` | `OPEN` | Klappe öffnen (Automatik 15 min pausiert) |
| `<base>/cmnd/door` | `CLOSE` | Klappe schließen (Automatik 5 min pausiert) |
| `<base>/cmnd/door` | `STOP` | Motor sofort stoppen |
| `<base>/cmnd/door` | `TOGGLE` | Je nach Zustand öffnen/schließen/stoppen |
| `<base>/cmnd/light` | `ON` / `OFF` / `TOGGLE` | Locklicht schalten (manuell) |
| `<base>/cmnd/stalllight` | `ON` / `OFF` / `TOGGLE` | Stalllicht schalten |
| `<base>/cmnd/settings` | JSON | Betriebseinstellungen anwenden |
| `<base>/cmnd/get` | `STATUS` | `tele/status` sofort publizieren |
| `<base>/cmnd/get` | `SETTINGS` | `tele/settings` sofort publizieren |
| `<base>/cmnd/get` | `LOG` | gesamtes Logbuch als `tele/log` senden |
| `<base>/cmnd/reboot` | `NOW` | ESP sauber neustarten |

### `cmnd/settings` JSON

Erlaubt das Setzen beliebiger Teilfelder. Felder, die nicht im JSON
stehen, bleiben unverändert.

```json
{
  "openMode": "light",
  "closeMode": "light",
  "openLightThreshold": 350,
  "closeLightThreshold": 15,
  "lampPreClose": 10,
  "lampPostClose": 15
}
```

**Regel:** Sind beide Modi `light`, muss `openLightThreshold` größer
sein als `closeLightThreshold`, sonst wird die Änderung abgelehnt und
der Fehler ins Log geschrieben (`MQTT Settings FEHLER: ...`).

Erlaubte Felder:

| Feld | Typ | Bemerkung |
|---|---|---|
| `openMode` | `"light"` / `"time"` | |
| `closeMode` | `"light"` / `"time"` | |
| `openTime` | `"HH:MM"` | nur bei `time` relevant |
| `closeTime` | `"HH:MM"` | nur bei `time` relevant |
| `openLightThreshold` | int (lx) | |
| `closeLightThreshold` | int (lx) | |
| `lampPreOpen` | int (Minuten) | Vorlicht Öffnen |
| `lampPostOpen` | int (Minuten) | Nachlicht Öffnen |
| `lampPreClose` | int (Minuten) | Vorlicht Schließen |
| `lampPostClose` | int (Minuten) | Nachlicht Schließen |

### Automatik pausieren

Jeder `OPEN`/`CLOSE`/`TOGGLE`‑Befehl setzt `manualOverrideUntil` für
5 min (Schließen) bzw. 15 min (Öffnen). In dieser Zeit wird die
Automatik nicht gegensteuern.

---

## Last Will & Testament

Die Klappe meldet sich beim Broker mit Will‑Topic
`<base>/tele/availability` und Will‑Payload `offline` an. Beim
erfolgreichen Connect wird direkt `online` (retained) publiziert,
beim sauberen Reboot (`cmnd/reboot NOW`) ebenfalls `offline`, bevor
`ESP.restart()` aufgerufen wird.

So lässt sich aus Home Assistant oder Node‑RED zuverlässig ermitteln,
ob die Klappe erreichbar ist.

---

## Beispiele

### Status abfragen

```bash
mosquitto_pub -h 192.168.1.100 -t huehnerklappe/cmnd/get -m STATUS
```

### Klappe öffnen

```bash
mosquitto_pub -h 192.168.1.100 -t huehnerklappe/cmnd/door -m OPEN
```

### Lux-Schwellen ändern

```bash
mosquitto_pub -h 192.168.1.100 -t huehnerklappe/cmnd/settings \
  -m '{"openLightThreshold":350,"closeLightThreshold":15}'
```

### Alles mithören (Debug)

```bash
mosquitto_sub -h 192.168.1.100 -t 'huehnerklappe/#' -v
```

### Home Assistant – Beispiel-Sensor (YAML)

```yaml
mqtt:
  sensor:
    - name: "Hühnerklappe Helligkeit"
      state_topic: "huehnerklappe/tele/status"
      value_template: "{{ value_json.light }}"
      unit_of_measurement: "lx"
      availability_topic: "huehnerklappe/tele/availability"

  binary_sensor:
    - name: "Hühnerklappe Tür"
      state_topic: "huehnerklappe/tele/status"
      value_template: "{{ value_json.door }}"
      payload_on: "Offen"
      payload_off: "Geschlossen"
      device_class: door
      availability_topic: "huehnerklappe/tele/availability"

  button:
    - name: "Hühnerklappe öffnen"
      command_topic: "huehnerklappe/cmnd/door"
      payload_press: "OPEN"
```
