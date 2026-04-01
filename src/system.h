#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <RTClib.h>

// ==================================================
// RTC
// ==================================================
extern RTC_DS3231 rtc;
extern bool       rtcOk;

// ==================================================
// OTA / Safe-State
// ==================================================
extern volatile bool otaInProgress;
extern volatile bool ioSafeState;

// ==================================================
// System-Health-Flags
// ==================================================
extern bool errorWifi;
extern bool errorMQTT;
extern bool errorSensor;   // true wenn VEML7700 ausgefallen

// Gibt true zurück wenn mind. ein kritischer Fehler vorliegt
bool systemError();

// Aktualisiert alle Health-Flags (WiFi, MQTT, Sensor)
void updateSystemHealth();
void tpl5110Init();
void wdogFeed();   // Software-Watchdog füttern – im Loop aufrufen

// Simulationszeit
DateTime nowRTC();                        // statt rtc.now() überall verwenden
void simSetOffset(int hours, int minutes); // Zeitoffset setzen (0,0 = deaktivieren)
bool simIsActive();
long simGetOffsetSeconds();

// Safe-State für OTA aktivieren / deaktivieren
void enterIoSafeState();
void leaveIoSafeState();
