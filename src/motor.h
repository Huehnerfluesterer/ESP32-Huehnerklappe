#pragma once
#include "types.h"
#include <Arduino.h>

// ==================================================
// MOTORSTEUERUNG – Klappe 1 (L298N Kanal A)
// ==================================================

extern MotorState    motorState;

// Tages-Statistik
extern int           statOpenCount;
extern int           statCloseCount;
extern unsigned long statOpenDurationMs;
extern unsigned long motorUntil;
extern String        motorReason;

// Motorpositionen (Laufzeit in ms, aus EEPROM geladen)
extern long openPosition;
extern long closePosition;

// Endschalter
extern bool         useLimitSwitches;
extern unsigned long limitOpenSince;
extern unsigned long limitCloseSince;

// Sperre: verhindert Automatik-Auslösung während manueller Aktion
extern bool actionLock;

// Initialisierung (LEDC-Kanal zuweisen, GPIO konfigurieren)
void motorInit();

// Hardware-Primitives
void motorStop();
void motorOpen();
void motorClose();

// Gesteuerte Starts mit Timeout
void startMotorOpen(unsigned long durationMs);
void startMotorClose(unsigned long durationMs);

// Rückwärtsfahren nach Blockade (kurz öffnen)
void reverseAfterBlockade();

// Gibt true zurück wenn Aktion manuell ausgelöst wurde (Taster/Web/MQTT)
bool isManualAction();

// Zyklischer Update: Endschalter + Timeout + Blockadeerkennung
void updateMotor();

// ACS712 Blockadeerkennung (gemeinsam für beide Kanäle)
extern float         currentBaseline;
extern bool          currentCalibrated;
extern unsigned long motorStartedAt;
extern bool          blockadeEnabled;
extern float         blockadeThresholdA;
extern float         peakCurrentA;

// Klappe 2 eigene Blockade-Schwelle
extern float         door2BlockadeThresholdA;

// ==================================================
// MOTORSTEUERUNG – Klappe 2 (L298N Kanal B)
// ==================================================

extern MotorState    motor2State;
extern unsigned long motor2Until;
extern String        motor2Reason;

extern long door2OpenPosition;
extern long door2ClosePosition;

extern bool         door2UseLimitSwitches;
extern unsigned long limit2OpenSince;
extern unsigned long limit2CloseSince;

extern bool actionLock2;
extern unsigned long motor2StartedAt;

// Prüft ob irgendein Motor gerade läuft (Mutual Exclusion für ACS712)
bool isAnyMotorRunning();

void motor2Init();
void motor2Stop();
void motor2Open();
void motor2Close();
void startMotor2Open(unsigned long durationMs);
void startMotor2Close(unsigned long durationMs);
void reverseAfterBlockade2();
bool isManualAction2();
void updateMotor2();
