#pragma once
#include "types.h"

// EEPROM-Adressen
#define EEPROM_SIZE             1024
#define EEPROM_MAGIC_ADDR       1023   // letztes Byte: Magic-Prüfbyte
#define EEPROM_MAGIC_VALUE      0xA7   // Wert = EEPROM wurde korrekt initialisiert
#define EEPROM_ADDR_SETTINGS      0    // Settings-Struct
#define EEPROM_ADDR_OPEN_POS    100    // long openPosition
#define EEPROM_ADDR_CLOSE_POS   104    // long closePosition
#define EEPROM_ADDR_LIMIT_SW    120    // bool useLimitSwitches
#define EEPROM_ADDR_MQTT        200    // MqttSettings-Struct (~171 Bytes)
#define EEPROM_ADDR_DOORSTATE   400    // bool doorOpen
#define EEPROM_ADDR_THEME       450    // char[10] theme
#define EEPROM_ADDR_BLOCKADE    460    // bool blockadeEnabled (1) + float blockadeThresholdA (4)
#define EEPROM_ADDR_BME_SOURCE  470    // uint8_t: 0=lokal, 1=ESP-NOW
#define EEPROM_ADDR_RELAY       480    // bool relayEnabled (1) + uint8_t[6] relayMac (6)
#define EEPROM_ADDR_RGB         490    // uint8_t r, g, b, w, brightness (5 Bytes)
#define EEPROM_ADDR_CLOSE_DELAY 496    // uint8_t closeDelayMin (0–30 min)
#define EEPROM_ADDR_STALL_AUTO_OFF  497   // bool stallLightAutoOff (1 Byte)
#define EEPROM_ADDR_STALL_MINUTES   498   // uint8_t stallLightMinutes (1 Byte)
#define EEPROM_ADDR_NIGHT_ALARM     500   // bool + uint8_t h + uint8_t m (3 Bytes)
#define EEPROM_ADDR_TELEGRAM    600    // TelegramSettings (~77 Bytes)

// Klappe 2
#define EEPROM_ADDR_DOOR2_SETTINGS  700    // Door2Settings-Struct (~30 Bytes)
#define EEPROM_ADDR_DOOR2_OPEN_POS  760    // long door2OpenPosition
#define EEPROM_ADDR_DOOR2_CLOSE_POS 764    // long door2ClosePosition
#define EEPROM_ADDR_DOOR2_STATE     770    // bool door2Open
#define EEPROM_ADDR_DOOR2_LIMIT_SW  771    // bool door2UseLimitSwitches
#define EEPROM_ADDR_DOOR2_CLOSE_DLY 772    // uint8_t door2CloseDelayMin
#define EEPROM_ADDR_DOOR2_BLOCKADE  774    // float door2BlockadeThresholdA (4 Bytes)
#define EEPROM_ADDR_LAST_FW         800    // char[16] letzte bekannte FW_VERSION (für OTA-Erkennung)

// Zugriff auf Settings (in storage.cpp definiert)
extern Settings         settings;
extern MqttSettings     mqttSettings;
extern TelegramSettings telegramSettings;

// Betriebseinstellungen (werden aus EEPROM geladen und im RAM gespiegelt)
extern String openMode;
extern String closeMode;
extern String openTime;
extern String closeTime;
extern int    openLightThreshold;
extern int    closeLightThreshold;
extern int    lampPreOpen;
extern int    lampPostOpen;
extern int    lampPreClose;
extern int    lampPostClose;
extern int    closeDelayMin;   // Verzögerung nach Lux-Schwelle vor Schließen (0–30 min)

extern String uiTheme;

// Motorpositionen
extern long openPosition;
extern long closePosition;

// Endschalter-Flag
extern bool useLimitSwitches;

// Klappe 2 Betriebseinstellungen (aus EEPROM)
extern Door2Settings door2Settings;
extern String door2OpenMode;
extern String door2CloseMode;
extern String door2OpenTime;
extern String door2CloseTime;
extern int    door2OpenLightThreshold;
extern int    door2CloseLightThreshold;
extern int    door2CloseDelayMin;

// EEPROM initialisieren (muss VOR allen load*()-Aufrufen geschehen)
void storageInit();

// Settings (Öffnen/Schließen/Licht/Lampen)
void saveSettings();
void loadSettings();
void applySettingsToRam();   // Struct → globale Variablen

// MQTT-Einstellungen
void saveMqttSettings();
void loadMqttSettings();

// Telegram-Einstellungen
void saveTelegramSettings();
void loadTelegramSettings();

// Türzustand
void saveDoorState();
void loadDoorState();

// Theme
void saveTheme(const String &theme);
void loadTheme();

void saveBlockadeSettings();
void loadBlockadeSettings();

// BME280 Quellmodus
void saveBmeSource();
void loadBmeSource();

// Relais ESP-NOW
void saveRelaySettings();
void loadRelaySettings();

// RGB Farbe & Helligkeit
void saveRgbSettings();
void loadRgbSettings();

// Schließverzögerung Lichtsensor
void saveCloseDelay();
void loadCloseDelay();

// Motor-Kalibrierung
void saveMotorPositions();
void loadMotorPositions();

// Endschalter-Einstellung
void loadLimitSwitchSetting();

// Stalllicht-Einstellungen
void saveStallLightSettings();
void loadStallLightSettings();

// Nacht-Alarm (Tür offen)
void saveNightAlarm();
void loadNightAlarm();

// Klappe 2
void saveDoor2Settings();
void loadDoor2Settings();
void applyDoor2SettingsToRam();
void saveDoor2State();
void loadDoor2State();
void saveDoor2MotorPositions();
void loadDoor2MotorPositions();
void loadDoor2LimitSwitchSetting();
void saveDoor2CloseDelay();
void loadDoor2CloseDelay();
void saveDoor2BlockadeThreshold();
void loadDoor2BlockadeThreshold();