#pragma once
#include <Arduino.h>
#include <WebServer.h>

extern WebServer server;

// Antwort-Helper mit Connection:close (iOS Safari Fix)
void sendHTML(const String &html, int code = 200);
void sendJSON(const String &json, int code = 200);
void sendPlain(const String &text, int code = 200);

// Chunked Transfer – Seite stückweise senden (TTFB → 0)
void sendHTMLStart(int code = 200);
void sendHTMLChunk(const String &chunk);
void sendHTMLEnd();

// Hilfsfunktionen (gemeinsam von allen Web-Dateien genutzt)
String renderThemeHead(const String &title);
String renderFooter();
int    timeToMinutes(const String &t);

// Startseite
void handleRoot();
void handleStatus();

// Einstellungen
void handleSettings();
void handleSaveOpen();
void handleSaveClose();

// Erweitert
void handleAdvanced();
void handleEspNow();
void handleRgb();
void handleBlockade();
void handleFw();
void handleSelftest();

// MQTT
void handleMqtt();
void handleSaveMqtt();

// Telegram
void handleTelegram();
void handleSaveTelegram();
void handleTelegramTest();

// Kalibrierung / Einlernen
void handleCalibration();
void handleLearn();
void handleLearnPage();

// Logbuch
void handleLogbook();

// Simulation
void handleSimulation();
void handleSimulationStatus();
void handleSimulationSet();
