#pragma once
#include <Arduino.h>

// ==================================================
// ENUMS
// ==================================================

enum DoorPhase
{
    PHASE_IDLE,     // Klappe steht, wartet
    PHASE_OPENING,  // Motor fährt auf
    PHASE_OPEN,     // Klappe offen
    PHASE_CLOSING   // Motor fährt zu
};

enum MotorState
{
    MOTOR_STOPPED,
    MOTOR_OPENING,
    MOTOR_CLOSING
};

enum LightState
{
    LIGHT_OFF,
    LIGHT_PRE_OPEN,    // Vor-Licht Öffnung
    LIGHT_POST_OPEN,   // Nach-Licht Öffnung
    LIGHT_PRE_CLOSE,   // Vor-Licht Schließen (inkl. Hold via Keep-Alive)
    LIGHT_POST_CLOSE   // Nach-Licht Schließen (mit Dimmung)
};

// ==================================================
// STRUCTS
// ==================================================

struct Settings
{
    char openMode[6];
    char closeMode[6];
    char openTime[6];
    char closeTime[6];
    int  openLightThreshold;
    int  closeLightThreshold;
    int  lampPreOpen;
    int  lampPostOpen;
    int  lampPreClose;
    int  lampPostClose;
};

struct MqttSettings
{
    bool     enabled;
    char     host[40];
    uint16_t port;
    char     user[32];
    char     pass[32];
    char     clientId[32];
    char     base[32];
};

struct TelegramSettings
{
    bool    enabled;
    char    token[48];     // Bot-Token (z.B. 123456789:ABCdef...)
    char    chatId[24];    // Ziel-Chat-ID (ggf. negativ für Gruppen)
    uint8_t deadlineH;     // Stunde: bis hier muss die Klappe offen sein (Alarm)
    uint8_t deadlineM;     // Minute
    bool    notifyOpen;    // Bei Öffnung Nachricht senden
    bool    notifyClose;   // Bei Schließung Nachricht senden
};
