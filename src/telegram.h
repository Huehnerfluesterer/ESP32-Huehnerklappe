#pragma once
#include <Arduino.h>

// Telegram initialisieren (einmalig in setup)
void telegramInit();

// Nachricht senden
void telegramSend(const String &msg);

// Ereignis-Nachrichten
void telegramDoorOpened(const String &reason);
void telegramDoorClosed(const String &reason, float lux);
void telegramSensorError();
void telegramWatchdogRestart();
void telegramDeadlineCheck();   // im loop() täglich um TELEGRAM_OPEN_DEADLINE prüfen
