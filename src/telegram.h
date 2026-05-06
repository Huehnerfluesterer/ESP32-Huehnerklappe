#pragma once
#include <Arduino.h>

// Telegram initialisieren (einmalig in setup)
void telegramInit();

// Roh-Senden (gibt true bei Erfolg zurück) – auch für Test-Button
bool telegramSendRaw(const String &text);

// Nachricht senden
void telegramSend(const String &msg);

// Ereignis-Nachrichten
void telegramDoorOpened(const String &reason);
void telegramDoorClosed(const String &reason, float lux);
void telegramSensorError();
void telegramWatchdogRestart();
void telegramDeadlineCheck();   // im loop() täglich um TELEGRAM_OPEN_DEADLINE prüfen
void telegramNightCheck();     // im loop() abends prüfen ob Tür noch offen
void telegramCheckPending();   // Verzögerte WDT-Benachrichtigung

// Nacht-Alarm Einstellungen (in EEPROM persistiert)
extern bool    nightAlarmEnabled;
extern uint8_t nightAlarmH;
extern uint8_t nightAlarmM;
