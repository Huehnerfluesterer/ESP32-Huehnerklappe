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
extern bool errorSensor;

bool systemError();
void updateSystemHealth();
void tpl5110Init();
void wdogFeed();   // Task-WDT direkt füttern (aus Loop aufrufen)

// Crash-Breadcrumb (überlebt Watchdog-Reset)
void setCrumb(uint8_t c);
String crumbName(uint8_t c);
#define CRUMB_IDLE          0
#define CRUMB_WEBSERVER     1
#define CRUMB_MQTT          2
#define CRUMB_WIFI          3
#define CRUMB_NTP           4
#define CRUMB_MOTOR         5
#define CRUMB_LUX           6
#define CRUMB_I2C_HEALTH    7
#define CRUMB_BME           8
#define CRUMB_LITTLEFS      9
#define CRUMB_AUTOMATIK    10
#define CRUMB_TELEGRAM     11
#define CRUMB_LIGHT        12
extern RTC_NOINIT_ATTR volatile uint8_t crashBreadcrumb;

// Restart-Quelle (überlebt Software-Reset in RTC-Memory)
#define RESTART_UNKNOWN     0
#define RESTART_VEML        2
#define RESTART_OTA         3
#define RESTART_MQTT_CMD    4
#define RESTART_WEB_UI      5
#define RESTART_HEAP_LOW    6
extern RTC_NOINIT_ATTR uint8_t lastRestartSource;
String restartSourceName(uint8_t s);

// Crash-Diagnostik (Werte werden laufend im Loop gesichert → RTC-Memory)
void diagReset();
void diagLogBootInfo();
void diagUpdate();

// Simulationszeit
DateTime nowRTC();
void simSetOffset(int hours, int minutes);
bool simIsActive();
long simGetOffsetSeconds();

// Safe-State für OTA
void enterIoSafeState();
void leaveIoSafeState();