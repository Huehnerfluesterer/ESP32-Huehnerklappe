#pragma once

// ==================================================
// STATUS-LED  –  WS2812 auf IO48
// ==================================================
// Zustände (Priorität absteigend):
//   OTA        → Blau, Lauflicht
//   Fehler     → Rot (WiFi: schnell / MQTT: langsam / Sensor+RTC: Orange)
//   WiFi-Init  → Gelb, langsam blinken
//   Alles OK   → Grün, kurz aufblitzen alle 3 s
// ==================================================

void statusLedInit();    // einmal in setup() aufrufen
void statusLedUpdate();  // jeden Loop-Durchlauf aufrufen
void statusLedOta();     // während OTA-Progress aufrufen