#pragma once
#include "types.h"
#include <Arduino.h>

extern bool         doorOpen;
extern DoorPhase    doorPhase;
extern unsigned long doorOpenedAt;

// Klappe 2 Zustand
extern bool         door2Open;
extern DoorPhase    door2Phase;

extern bool         learningActive;
extern bool         learningOpenDone;
extern unsigned long learnStartTime;
extern bool         openedByLight;
extern unsigned long manualOverrideUntil;

extern bool         preLightOpenDone;
extern bool         preLightCloseDone;
extern unsigned long lightAboveSince;
extern unsigned long lightBelowSince;
extern unsigned long plannedCloseAt;

// Taster-Logik (zyklisch aufrufen)
void updateButton();          // BUTTON_PIN: Kurz → Klappe 1, Lang (700ms) → Klappe 2
void updateButton2();         // BUTTON2_PIN: dedizierter Klappe-2-Taster
void updateStallButton();
void updateRedButton();       // Taster Rotlicht (Toggle RGB-Rot)
void handleButtonPress();     // Klappe 1 Toggle-Aktion
void handleButton2Press();    // Klappe 2 Toggle-Aktion (gemeinsam genutzt: Langdruck K1 + K2-Taster)