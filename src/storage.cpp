#include "storage.h"
#include "door.h"    // doorOpen, doorPhase, door2Open, door2Phase
#include "motor.h"   // blockadeEnabled, blockadeThresholdA, door2OpenPosition, ...
#include "pins.h"    // BLOCKADE_THRESHOLD_A
#include "bme.h"     // bmeSource
#include "relay.h"   // relayEnabled, relayMac
#include "light.h"   // rgbColorR/G/B, rgbBrightness
#include "telegram.h" // nightAlarmEnabled/H/M
#include "types.h"
#include <EEPROM.h>
#include <Arduino.h>

// ==================================================
// GLOBALE VARIABLEN (dieses Moduls)
// ==================================================
Settings     settings;
MqttSettings mqttSettings;
TelegramSettings telegramSettings;

String openMode  = "time";
String closeMode = "time";
String openTime  = "07:00";
String closeTime = "20:00";

int openLightThreshold  = 300;
int closeLightThreshold = 200;

int lampPreOpen   = 5;
int lampPostOpen  = 5;
int lampPreClose  = 5;
int lampPostClose = 5;
int closeDelayMin = 0;  // Verzögerung nach Lux-Schwelle (0 = sofort, wie bisher)

String uiTheme = "auto";

long openPosition  = 6000;
long closePosition = 6000;

bool useLimitSwitches = true;

// ==================================================
void storageInit()
{
    EEPROM.begin(EEPROM_SIZE);

    // Magic-Byte prüfen – wenn fehlt → EEPROM frisch oder nach OTA korrupt
    uint8_t magic = EEPROM.read(EEPROM_MAGIC_ADDR);
    if (magic != EEPROM_MAGIC_VALUE) {
        Serial.println("⚠️ EEPROM Magic fehlt – behalte vorhandene Werte, setze nur fehlende Defaults");
        // Motorpositionen prüfen und ggf. Default setzen
        long op, cp;
        EEPROM.get(EEPROM_ADDR_OPEN_POS,  op);
        EEPROM.get(EEPROM_ADDR_CLOSE_POS, cp);
        if (op  < 1000 || op  > 20000) { op  = 6000; EEPROM.put(EEPROM_ADDR_OPEN_POS,  op); }
        if (cp  < 1000 || cp  > 20000) { cp  = 6000; EEPROM.put(EEPROM_ADDR_CLOSE_POS, cp); }
        // Magic setzen
        EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VALUE);
        EEPROM.commit();
        Serial.printf("✅ EEPROM initialisiert – openPos=%ld closePos=%ld\n", op, cp);
    }
}

void saveStallLightSettings()
{
    EEPROM.put(EEPROM_ADDR_STALL_AUTO_OFF, stallLightAutoOff);
    EEPROM.put(EEPROM_ADDR_STALL_MINUTES,  (uint8_t)stallLightMinutes);
    EEPROM.commit();
}

void loadStallLightSettings()
{
    bool ao; uint8_t min;
    EEPROM.get(EEPROM_ADDR_STALL_AUTO_OFF, ao);
    EEPROM.get(EEPROM_ADDR_STALL_MINUTES,  min);
    if (ao != true && ao != false) ao = true;  // EEPROM-Garbage → Default
    if (min < 1 || min > 120) min = 1;
    stallLightAutoOff  = ao;
    stallLightMinutes  = min;
}
// ==================================================
// SETTINGS
// ==================================================
void applySettingsToRam()
{
    openMode  = settings.openMode;
    closeMode = settings.closeMode;
    openTime  = settings.openTime;
    closeTime = settings.closeTime;

    openLightThreshold  = settings.openLightThreshold;
    closeLightThreshold = settings.closeLightThreshold;

    lampPreOpen   = settings.lampPreOpen;
    lampPostOpen  = settings.lampPostOpen;
    lampPreClose  = settings.lampPreClose;
    lampPostClose = settings.lampPostClose;
}

void saveSettings()
{
    EEPROM.put(EEPROM_ADDR_SETTINGS, settings);
    EEPROM.commit();
}

void loadSettings()
{
    memset(&settings, 0, sizeof(settings));
    EEPROM.get(EEPROM_ADDR_SETTINGS, settings);

    if (strlen(settings.openMode) == 0)
    {
        // Standardwerte
        strcpy(settings.openMode,  "time");
        strcpy(settings.closeMode, "time");
        strcpy(settings.openTime,  "07:00");
        strcpy(settings.closeTime, "20:00");
        settings.openLightThreshold  = 300;
        settings.closeLightThreshold = 200;
        settings.lampPreOpen   = 5;
        settings.lampPostOpen  = 5;
        settings.lampPreClose  = 5;
        settings.lampPostClose = 5;
        saveSettings();
    }

    applySettingsToRam();
}

// ==================================================
// MQTT
// ==================================================
void saveMqttSettings()
{
    EEPROM.put(EEPROM_ADDR_MQTT, mqttSettings);
    EEPROM.commit();
}

void loadMqttSettings()
{
    EEPROM.get(EEPROM_ADDR_MQTT, mqttSettings);

    // Prüfen ob host druckbare ASCII-Zeichen enthält (kein Garbage)
    bool hostValid = true;
    for (int i = 0; i < (int)sizeof(mqttSettings.host); i++) {
        char c = mqttSettings.host[i];
        if (c == '\0') break;                        // Stringende → OK
        if (c < 32 || c > 126) { hostValid = false; break; }  // Kein druckbares ASCII → Garbage
    }

    if (!hostValid || strcmp(mqttSettings.host, "0.0.0.0") == 0)
    {
        memset(&mqttSettings, 0, sizeof(mqttSettings));
        mqttSettings.enabled = false;
        mqttSettings.port    = 1883;
        // alle Strings bleiben leer (\0 durch memset)
        saveMqttSettings();
    }

    // Port-Sanity (65535 = uninitialisiert)
    if (mqttSettings.port == 0 || mqttSettings.port == 65535)
        mqttSettings.port = 1883;
}

// ==================================================
// TELEGRAM
// ==================================================
void saveTelegramSettings()
{
    EEPROM.put(EEPROM_ADDR_TELEGRAM, telegramSettings);
    EEPROM.commit();
}

void loadTelegramSettings()
{
    EEPROM.get(EEPROM_ADDR_TELEGRAM, telegramSettings);

    // Sanity: Token muss druckbares ASCII sein, sonst Defaults
    bool ok = true;
    for (size_t i = 0; i < sizeof(telegramSettings.token); i++) {
        char c = telegramSettings.token[i];
        if (c == '\0') break;
        if (c < 32 || c > 126) { ok = false; break; }
    }
    if (!ok ||
        telegramSettings.deadlineH > 23 ||
        telegramSettings.deadlineM > 59)
    {
        memset(&telegramSettings, 0, sizeof(telegramSettings));
        telegramSettings.enabled     = false;
        telegramSettings.deadlineH   = 9;
        telegramSettings.deadlineM   = 0;
        telegramSettings.notifyOpen  = true;
        telegramSettings.notifyClose = true;
        saveTelegramSettings();
    }
}

// ==================================================
// TÜRZUSTAND
// ==================================================
void saveDoorState()
{
    EEPROM.put(EEPROM_ADDR_DOORSTATE, doorOpen);
    EEPROM.commit();
}

void loadDoorState()
{
    EEPROM.get(EEPROM_ADDR_DOORSTATE, doorOpen);
    if (doorOpen != true && doorOpen != false)
    {
        doorOpen  = false;
        doorPhase = PHASE_IDLE;
        saveDoorState();
    }
}

// ==================================================
// THEME
// ==================================================
void saveTheme(const String &theme)
{
    uiTheme = theme;
    char buf[10];
    theme.toCharArray(buf, sizeof(buf));
    EEPROM.put(EEPROM_ADDR_THEME, buf);
    EEPROM.commit();
}

void loadTheme()
{
    char buf[10] = {0};
    EEPROM.get(EEPROM_ADDR_THEME, buf);
    uiTheme = (strlen(buf) == 0) ? "auto" : String(buf);
}

// ==================================================
// MOTORPOSITIONEN
// ==================================================
void saveMotorPositions()
{
    EEPROM.put(EEPROM_ADDR_OPEN_POS,  openPosition);
    EEPROM.put(EEPROM_ADDR_CLOSE_POS, closePosition);
    EEPROM.commit();
}

void loadMotorPositions()
{
    EEPROM.get(EEPROM_ADDR_OPEN_POS,  openPosition);
    EEPROM.get(EEPROM_ADDR_CLOSE_POS, closePosition);

    // Sanitize – Bereich 500ms bis 60s
    if (openPosition  < 500  || openPosition  > 60000) openPosition  = 6000;
    if (closePosition < 500  || closePosition > 60000) closePosition = 6000;
    Serial.printf("📦 EEPROM: openPos=%ld ms, closePos=%ld ms\n", openPosition, closePosition);
}

void loadLimitSwitchSetting()
{
    EEPROM.get(EEPROM_ADDR_LIMIT_SW, useLimitSwitches);
    if (useLimitSwitches != true && useLimitSwitches != false)
        useLimitSwitches = true;
}

void saveBlockadeSettings()
{
    EEPROM.put(EEPROM_ADDR_BLOCKADE,     blockadeEnabled);
    EEPROM.put(EEPROM_ADDR_BLOCKADE + 1, blockadeThresholdA);
    EEPROM.commit();
}

void loadBlockadeSettings()
{
    EEPROM.get(EEPROM_ADDR_BLOCKADE,     blockadeEnabled);
    EEPROM.get(EEPROM_ADDR_BLOCKADE + 1, blockadeThresholdA);
    // Sanitize
    if (blockadeEnabled != true && blockadeEnabled != false)
        blockadeEnabled = true;
    if (isnan(blockadeThresholdA) || blockadeThresholdA < 0.5f || blockadeThresholdA > 10.0f)
        blockadeThresholdA = 2.0f;
}

void saveBmeSource()
{
    uint8_t val = (uint8_t)bmeSource;
    EEPROM.put(EEPROM_ADDR_BME_SOURCE, val);
    EEPROM.commit();
}

void loadBmeSource()
{
    uint8_t val = 0;
    EEPROM.get(EEPROM_ADDR_BME_SOURCE, val);
    // Sanitize: nur 0 oder 1 gültig
    bmeSource = (val == 1) ? BME_SOURCE_ESPNOW : BME_SOURCE_LOCAL;
}

void saveRelaySettings()
{
    EEPROM.put(EEPROM_ADDR_RELAY,     relayEnabled);
    EEPROM.put(EEPROM_ADDR_RELAY + 1, relayMac);
    EEPROM.commit();
}

void loadRelaySettings()
{
    EEPROM.get(EEPROM_ADDR_RELAY,     relayEnabled);
    EEPROM.get(EEPROM_ADDR_RELAY + 1, relayMac);
    // Sanitize
    if (relayEnabled != true) relayEnabled = false;
}

void saveRgbSettings()
{
    EEPROM.put(EEPROM_ADDR_RGB,     rgbColorR);
    EEPROM.put(EEPROM_ADDR_RGB + 1, rgbColorG);
    EEPROM.put(EEPROM_ADDR_RGB + 2, rgbColorB);
    EEPROM.put(EEPROM_ADDR_RGB + 3, rgbColorW);
    EEPROM.put(EEPROM_ADDR_RGB + 4, rgbBrightness);
    EEPROM.commit();
}

void loadRgbSettings()
{
    uint8_t r, g, b, w, br;
    EEPROM.get(EEPROM_ADDR_RGB,     r);
    EEPROM.get(EEPROM_ADDR_RGB + 1, g);
    EEPROM.get(EEPROM_ADDR_RGB + 2, b);
    EEPROM.get(EEPROM_ADDR_RGB + 3, w);
    EEPROM.get(EEPROM_ADDR_RGB + 4, br);

    // Default wenn:
    // - alle 0xFF (nie beschrieben)
    // - alle 0 (FW-Update hat EEPROM-Bereich genullt)
    bool allFF  = (r == 0xFF && g == 0xFF && b == 0xFF && w == 0xFF);
    bool allZero = (r == 0 && g == 0 && b == 0 && w == 0);

    if (allFF || allZero) {
        // Warm-Weiß Default: nur W-Kanal aktiv
        rgbColorR    = 0;
        rgbColorG    = 0;
        rgbColorB    = 0;
        rgbColorW    = 255;
        rgbBrightness = 255;
        saveRgbSettings();  // sofort speichern damit nächster Start korrekt ist
        Serial.println("RGB Default: Warm-Weiß (W=255)");
    } else {
        rgbColorR    = r;
        rgbColorG    = g;
        rgbColorB    = b;
        rgbColorW    = w;
        rgbBrightness = (br == 0) ? 255 : br;
    }
}

void saveCloseDelay()
{
    uint8_t val = (uint8_t)constrain(closeDelayMin, 0, 30);
    EEPROM.put(EEPROM_ADDR_CLOSE_DELAY, val);
    EEPROM.commit();
}

void loadCloseDelay()
{
    uint8_t val;
    EEPROM.get(EEPROM_ADDR_CLOSE_DELAY, val);
    if (val == 0xFF) val = 0;  // nie beschrieben → 0 (kein Delay)
    closeDelayMin = constrain((int)val, 0, 30);
}

void saveNightAlarm()
{
    EEPROM.put(EEPROM_ADDR_NIGHT_ALARM,     nightAlarmEnabled);
    EEPROM.put(EEPROM_ADDR_NIGHT_ALARM + 1, nightAlarmH);
    EEPROM.put(EEPROM_ADDR_NIGHT_ALARM + 2, nightAlarmM);
    EEPROM.commit();
}

void loadNightAlarm()
{
    bool en; uint8_t h, m;
    EEPROM.get(EEPROM_ADDR_NIGHT_ALARM,     en);
    EEPROM.get(EEPROM_ADDR_NIGHT_ALARM + 1, h);
    EEPROM.get(EEPROM_ADDR_NIGHT_ALARM + 2, m);
    if (en != true && en != false) en = false;
    if (h > 23) h = 23;
    if (m > 59) m = 0;
    nightAlarmEnabled = en;
    nightAlarmH       = h;
    nightAlarmM       = m;
}

// ==================================================
// KLAPPE 2 – Einstellungen
// ==================================================
Door2Settings door2Settings;

String door2OpenMode  = "time";
String door2CloseMode = "time";
String door2OpenTime  = "07:00";
String door2CloseTime = "20:00";

int door2OpenLightThreshold  = 300;
int door2CloseLightThreshold = 200;
int door2CloseDelayMin       = 0;

void applyDoor2SettingsToRam()
{
    door2OpenMode  = door2Settings.openMode;
    door2CloseMode = door2Settings.closeMode;
    door2OpenTime  = door2Settings.openTime;
    door2CloseTime = door2Settings.closeTime;
    door2OpenLightThreshold  = door2Settings.openLightThreshold;
    door2CloseLightThreshold = door2Settings.closeLightThreshold;
}

void saveDoor2Settings()
{
    EEPROM.put(EEPROM_ADDR_DOOR2_SETTINGS, door2Settings);
    EEPROM.commit();
}

void loadDoor2Settings()
{
    memset(&door2Settings, 0, sizeof(door2Settings));
    EEPROM.get(EEPROM_ADDR_DOOR2_SETTINGS, door2Settings);

    if (strlen(door2Settings.openMode) == 0)
    {
        strcpy(door2Settings.openMode,  "time");
        strcpy(door2Settings.closeMode, "time");
        strcpy(door2Settings.openTime,  "07:00");
        strcpy(door2Settings.closeTime, "20:00");
        door2Settings.openLightThreshold  = 300;
        door2Settings.closeLightThreshold = 200;
        saveDoor2Settings();
    }
    applyDoor2SettingsToRam();
}

void saveDoor2State()
{
    EEPROM.put(EEPROM_ADDR_DOOR2_STATE, door2Open);
    EEPROM.commit();
}

void loadDoor2State()
{
    EEPROM.get(EEPROM_ADDR_DOOR2_STATE, door2Open);
    if (door2Open != true && door2Open != false)
    {
        door2Open  = false;
        door2Phase = PHASE_IDLE;
        saveDoor2State();
    }
}

void saveDoor2MotorPositions()
{
    EEPROM.put(EEPROM_ADDR_DOOR2_OPEN_POS,  door2OpenPosition);
    EEPROM.put(EEPROM_ADDR_DOOR2_CLOSE_POS, door2ClosePosition);
    EEPROM.commit();
}

void loadDoor2MotorPositions()
{
    EEPROM.get(EEPROM_ADDR_DOOR2_OPEN_POS,  door2OpenPosition);
    EEPROM.get(EEPROM_ADDR_DOOR2_CLOSE_POS, door2ClosePosition);
    if (door2OpenPosition  < 500  || door2OpenPosition  > 60000) door2OpenPosition  = 6000;
    if (door2ClosePosition < 500  || door2ClosePosition > 60000) door2ClosePosition = 6000;
    Serial.printf("📦 EEPROM Klappe2: openPos=%ld ms, closePos=%ld ms\n", door2OpenPosition, door2ClosePosition);
}

void loadDoor2LimitSwitchSetting()
{
    EEPROM.get(EEPROM_ADDR_DOOR2_LIMIT_SW, door2UseLimitSwitches);
    if (door2UseLimitSwitches != true && door2UseLimitSwitches != false)
        door2UseLimitSwitches = false;
}

void saveDoor2CloseDelay()
{
    uint8_t val = (uint8_t)constrain(door2CloseDelayMin, 0, 30);
    EEPROM.put(EEPROM_ADDR_DOOR2_CLOSE_DLY, val);
    EEPROM.commit();
}

void loadDoor2CloseDelay()
{
    uint8_t val;
    EEPROM.get(EEPROM_ADDR_DOOR2_CLOSE_DLY, val);
    if (val == 0xFF) val = 0;
    door2CloseDelayMin = constrain((int)val, 0, 30);
}

void saveDoor2BlockadeThreshold()
{
    EEPROM.put(EEPROM_ADDR_DOOR2_BLOCKADE, door2BlockadeThresholdA);
    EEPROM.commit();
}

void loadDoor2BlockadeThreshold()
{
    EEPROM.get(EEPROM_ADDR_DOOR2_BLOCKADE, door2BlockadeThresholdA);
    if (isnan(door2BlockadeThresholdA) || door2BlockadeThresholdA < 0.5f || door2BlockadeThresholdA > 10.0f)
        door2BlockadeThresholdA = BLOCKADE_THRESHOLD_A;
}
