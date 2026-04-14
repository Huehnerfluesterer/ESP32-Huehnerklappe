# ⚡ Blockadeerkennung (ACS712)

🇩🇪 Deutsch · Firmware `3.0.16`

Die Blockadeerkennung misst den **Motorstrom** über einen ACS712‑Hall‑
Sensor und stoppt den Motor, sobald der Strom einen einstellbaren
Schwellwert über der gelernten Baseline überschreitet. So wird ein
klemmender Motor (Huhn, Ast, Eis) abgefangen, bevor Getriebe oder
Schnur Schaden nehmen.

---

## Inhaltsverzeichnis

- [Funktionsprinzip](#-funktionsprinzip)
- [Hardware](#-hardware)
- [Begriffe: Baseline, Peak, Schwellwert](#-begriffe-baseline-peak-schwellwert)
- [Einstellung im WebUI](#-einstellung-im-webui)
- [Schritt‑für‑Schritt Kalibrierung](#-schritt-für-schritt-kalibrierung)
- [Endpoints](#-endpoints)
- [Fehlerdiagnose](#-fehlerdiagnose)

---

## 🧠 Funktionsprinzip

Der ACS712 liefert eine zum Motorstrom proportionale Spannung, die
ratiometrisch um `VCC/2` (≈ 2,5 V bei 5 V) schwankt. Über einen
Spannungsteiler 2/3 landet dieses Signal auf `GPIO 7` (`ACS712_PIN`),
wo der ADC1 des ESP32 es liest.

Im Firmware‑Code (`pins.h`):

```cpp
#define ACS712_PIN            7
#define ACS712_MV_PER_A       185      // ACS712‑5A → 185 mV/A
#define ACS712_VCC            5.0f
#define ACS712_ZERO_V         2.5f     // Leerlauf‑Offset
#define BLOCKADE_THRESHOLD_A  2.0f     // Default‑Wert (EEPROM überschreibt)
```

Die Baseline (siehe unten) wird **automatisch** in den ersten Sekunden
eines Motorlaufs gelernt, der aktuelle Strom wird kontinuierlich
gemessen, und sobald `|I| > Baseline + Schwellwert` überschritten
wird, stoppt der Motor sofort mit Log‑Eintrag.

---

## 🔌 Hardware

Verkabelung des ACS712:

```
ACS712 VCC ──── 5 V
ACS712 GND ──── GND
ACS712 OUT ──── [20 kΩ]──┬──[30 kΩ]──── GND
                          │
                          └──────────── ESP32-S3 GPIO 7

Motor-Leitung ─ ACS712 IP+  ────→  IP−  ─ L298N OUTx
```

Der Teiler `20 kΩ / 30 kΩ` bringt den maximalen Swing (0–5 V) auf
0–3 V und bleibt damit im ADC‑Bereich des ESP32. Das Verhältnis ist
im Code hart kodiert:

```cpp
// aus handleBlockade()
float vMeas   = (sum / S) * (3.3f / 4095.0f);
float vSensor = vMeas / (20.0f / 30.0f);    // ≈ × 1,5 zurückrechnen
float amps    = fabsf((vSensor - ACS712_ZERO_V) / (ACS712_MV_PER_A / 1000.0f));
```

> **Wichtig:** Wenn du einen anderen Teiler verwendest, muss der
> Faktor `20.0f / 30.0f` entsprechend angepasst werden.

GPIO 7 wurde bewusst gewählt, weil er zu **ADC1** gehört – ADC2 ist
auf dem ESP32 während aktiver WLAN‑Verbindung nicht zuverlässig
nutzbar.

---

## 📊 Begriffe: Baseline, Peak, Schwellwert

| Begriff | Wo im Code | Bedeutung |
|---|---|---|
| **Baseline** (`currentBaseline`) | wird beim Motorstart ermittelt | Normaler Leerlaufstrom beim freien Lauf |
| **Peak** (`peakCurrentA`) | läuft im Hintergrund mit | Höchster je gemessener Wert seit letztem Reset |
| **Schwellwert** (`blockadeThresholdA`) | aus EEPROM, WebUI | Zulässiger Abstand über Baseline |
| **Aktiver Auslöser** | `currentBaseline + blockadeThresholdA` | Bei Überschreiten stoppt der Motor |

**Warum Baseline + Offset und nicht fester Strom?**
Weil der Leerlaufstrom je nach Motor, Temperatur und Spannung schwankt.
Ein fester Wert müsste entweder sehr hoch (→ unsicher) oder sehr knapp
am Normalwert (→ Fehlauslösungen) liegen. Baseline + Δ ist robust.

---

## ⚙ Einstellung im WebUI

Im Browser **`/blockade`** öffnen. Die Seite zeigt:

- **Aktueller Strom** (Live‑Messung bei stehendem Motor)
- **Eingemessene Baseline** (oder `--` wenn der Motor noch nicht lief)
- **Höchster gemessener Strom** (Peak, rot hervorgehoben)
- **Aktiver Schwellwert** = `Baseline + Einstellung`
- Toggle **Blockadeerkennung aktiv**
- Eingabefeld **Schwellwert über Baseline (A)**

Live‑Anzeige aktualisiert sich sekündlich per AJAX (`/blockade-live`,
`/blockade-peak`, `/blockade-baseline`).

---

## 🧪 Schritt-für-Schritt Kalibrierung

So bekommst du einen vernünftigen Schwellwert:

1. **Motor einmal normal laufen lassen** – die Klappe komplett öffnen
   oder schließen, ohne zu blockieren. Die Baseline wird dabei
   automatisch gelernt.
2. **Höchsten Strom ablesen** (`Höchster gemessener Strom` auf der
   Seite). Das ist dein Normalwert, z. B. `0,8 A`.
3. **Peak zurücksetzen** über den Button *↺ Peak zurücksetzen*
   (`POST /blockade-peak-reset`).
4. **Klappe kurz von Hand blockieren** (Finger am Seil, Klappe
   festhalten) und einen Motorlauf auslösen. Der Strom wird deutlich
   höher gehen, z. B. `3,5 A`.
5. **Neuen Peak ablesen** (Blockade‑Wert).
6. **Schwellwert wählen:**
   ```
   Schwellwert ≈ (Blockade‑Peak − Normalwert) ÷ 2
              = (3,5 A − 0,8 A) ÷ 2
              = 1,4 A
   ```
   → Der Motor stoppt dann ab einem Absolutstrom von
   `Baseline + 1,4 A = 2,2 A`, also deutlich unter dem echten
   Blockade‑Strom, aber weit genug vom Normalbetrieb entfernt.

**Als Richtwert** funktioniert auch die Faustformel
„Leerlaufstrom × 4 bis 5" – bei einem kleinen Getriebemotor mit
0,3 A Leerlauf also `0,3 × 4 = 1,2 A` als Schwellwert.

**Zu viele Fehlauslösungen?** → Schwellwert erhöhen.
**Motor verklemmt zu lange?** → Schwellwert verringern.

---

## 🌐 Endpoints

Alle im Code registrierten Routen rund um die Blockadeerkennung:

| Route | Methode | Zweck |
|---|---|---|
| `/blockade` | GET | HTML‑Seite |
| `/blockade-live` | GET | Aktueller Strom (A, Plain Text) |
| `/blockade-peak` | GET | Höchster gemessener Strom |
| `/blockade-baseline` | GET | Aktuelle Baseline (oder `--`) |
| `/blockade-peak-reset` | POST | Peak auf 0 zurücksetzen |
| `/save-blockade` | POST | `enabled=0\|1&threshold=<A>` |

Die Einstellungen (`blockadeEnabled`, `blockadeThresholdA`) liegen im
EEPROM ab Adresse `EEPROM_ADDR_BLOCKADE = 460`.

---

## 🔍 Fehlerdiagnose

### Peak steht dauerhaft auf 0 A

- Motor wurde seit dem letzten Reboot noch nicht aktiv bewegt
- Im WebUI `Blockadeerkennung aktiv` prüfen
- Spannungsteiler/Verkabelung ACS712 → GPIO 7 kontrollieren
- ACS712 an 5 V (nicht 3,3 V) angeschlossen?

### Live-Strom steht auf ~0 A bei laufendem Motor

Der Live‑Messwert auf der Seite wird **nur bei gestopptem Motor**
berechnet (siehe `handleBlockade()`). Während der Motor läuft, zeigt
das Feld also absichtlich 0 – der Wert während des Laufs steckt im
Peak und in der Auslöse‑Logik im Hintergrund.

### Motor stoppt ständig ohne echte Blockade

- Schwellwert **zu niedrig** → auf der `/blockade`‑Seite erhöhen
- Baseline wurde bei einem zufällig hohen Startpeak gelernt → Klappe
  einmal vollständig durchlaufen lassen, dann Peak zurücksetzen und
  neu kalibrieren
- Spannungseinbruch am 12 V‑Netzteil unter Last → kräftigeres Netzteil

### Motor stoppt gar nicht trotz Blockade

- Toggle `Blockadeerkennung aktiv` eingeschaltet?
- Schwellwert realistisch? (Bei `10 A` löst fast nichts mehr aus)
- Kabel vom ACS712 OUT zum ESP lose?
- `blockade-live` im Browser öffnen und Motor blockieren → zeigt der
  Wert überhaupt einen Anstieg?

### Werte unplausibel (Rauschen, negative Werte)

Der Code nutzt `fabsf(...)`, sodass negative Ablesungen in positive
umgedreht werden. Bleiben die Werte trotzdem stark verrauscht, liegt
das meistens an gemeinsamen Masseproblemen. Wichtig: **ACS712‑GND,
ESP‑GND und Netzteil‑GND müssen am selben Punkt zusammenlaufen**.
Ein zusätzlicher 100 nF Kondensator direkt am VCC/GND des ACS712 hilft
gegen Schaltrauschen vom L298N.
