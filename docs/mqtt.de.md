# 📡 MQTT

🇩🇪 Deutsch

---

## Konfiguration

MQTT wird unter `/mqtt` im Web-Interface konfiguriert:

| Feld | Beschreibung | Beispiel |
|---|---|---|
| Host | IP oder Hostname des Brokers | `192.168.1.100` |
| Port | Standard MQTT-Port | `1883` |
| Client-ID | Eindeutige ID | `huehnerklappe` |
| Base-Topic | Präfix für alle Topics | `huehnerklappe` |
| Benutzer | Optional | |
| Passwort | Optional | |

---

## Topics

### Status (Subscribe / Empfangen)

| Topic | Payload | Beschreibung |
|---|---|---|
| `<base>/status` | JSON | Kompletter Systemstatus (alle 30 s) |
| `<base>/door` | `open` / `closed` / `opening` / `closing` | Türzustand |
| `<base>/lux` | Zahl | Aktueller Lux-Wert |
| `<base>/light` | `on` / `off` | Locklicht-Status |

### Befehle (Publish / Senden)

| Topic | Payload | Beschreibung |
|---|---|---|
| `<base>/cmd/door` | `open` / `close` / `stop` | Tür steuern |
| `<base>/cmd/light` | `on` / `off` | Locklicht schalten |
| `<base>/cmd/stall` | `on` / `off` | Stalllicht schalten |

---

## Status-JSON

```json
{
  "fw": "1.0.16",
  "uptime": 3600,
  "door": "open",
  "lux": 1250.5,
  "luxRate": -15.2,
  "lightState": "off",
  "wifiRssi": -62,
  "heap": 274056,
  "openMode": "lux",
  "closeMode": "lux"
}
```
