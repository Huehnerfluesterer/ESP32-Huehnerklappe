#include "light.h"
#include "logger.h"
#include "storage.h"
#include "pins.h"
#include <Arduino.h>

// ==================================================
// GLOBALE VARIABLEN
// ==================================================
LightState    lightState       = LIGHT_OFF;
unsigned long lightStateUntil  = 0;
bool          lightActive      = false;
bool          manualLightActive = false;

bool          stallLightActive  = false;
unsigned long stallLightOffTime = 0;

bool          rgbRedActive      = false;
uint8_t       rgbRedBrightness   = 255;

bool          dimmingActive     = false;
unsigned long dimStartTime      = 0;
unsigned long dimEndTime        = 0;

bool stallLightAutoOff = true;   // Standard: Auto-Aus aktiv

// LEDC-Kanäle (fest vergeben, Core 2.x)
static const int chR = 0;
static const int chG = 1;
static const int chB = 2;
static const int chW = 5;  // W-Kanal (RGBWW) – Kanal 5, Motor=3, RGB=0-2

// Konfigurierbare Lichtfarbe (änderbar über Web-UI /rgb)
uint8_t rgbColorR      = 255;
uint8_t rgbColorG      = 197;
uint8_t rgbColorB      = 143;
uint8_t rgbColorW      =   0;  // W-Kanal default aus
uint8_t rgbBrightness  = 255;

// ==================================================
// INITIALISIERUNG
// ==================================================
void lightInit()
{
    // Core 2.x API: ledcSetup + ledcAttachPin
    ledcSetup(chR, RGB_FREQ, RGB_BITS);
    ledcSetup(chG, RGB_FREQ, RGB_BITS);
    ledcSetup(chB, RGB_FREQ, RGB_BITS);
    ledcSetup(chW, RGB_FREQ, RGB_BITS);
    ledcAttachPin(RGB_PIN_R, chR);
    ledcAttachPin(RGB_PIN_G, chG);
    ledcAttachPin(RGB_PIN_B, chB);
    ledcAttachPin(RGB_PIN_W, chW);
    Serial.printf("✅ RGBW LEDC R=%d G=%d B=%d W=%d\n", chR, chG, chB, chW);

    // Sicher aus beim Start
    ledcWrite(chR, 0); ledcWrite(chG, 0); ledcWrite(chB, 0); ledcWrite(chW, 0);
}

// ==================================================
// INTERNE HELFER
// ==================================================
static void rgbSet(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0)
{
    ledcWrite(chR, r);
    ledcWrite(chG, g);
    ledcWrite(chB, b);
    ledcWrite(chW, w);
}

static void rgbSetScaled(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness, uint8_t w = 0)
{
    rgbSet(
        (uint16_t)r * brightness / 255,
        (uint16_t)g * brightness / 255,
        (uint16_t)b * brightness / 255,
        (uint16_t)w * brightness / 255
    );
}

static void rgbOff()
{
    rgbSet(0, 0, 0);
}

// ==================================================
// LOCKLICHT
// ==================================================
void lightOn()
{
    digitalWrite(RELAIS_PIN, RELAY_ON);
    rgbSetScaled(rgbColorR, rgbColorG, rgbColorB, rgbBrightness, rgbColorW);
    Serial.println("💡 LICHT EIN");
}

void lightOff()
{
    digitalWrite(RELAIS_PIN, RELAY_OFF);
    rgbOff();
    dimmingActive = false;
    lightState    = LIGHT_OFF;
    Serial.println("💡 LICHT AUS");
}

// ==================================================
// STALLLICHT
// ==================================================
void stallLightOn()
{
    stallLightActive = true;
    // Timer nur setzen wenn Auto-Aus aktiviert
    if (stallLightAutoOff)
        stallLightOffTime = millis() + (unsigned long)stallLightMinutes * 60000UL;
    else
        stallLightOffTime = 0;  // kein Timer

    digitalWrite(STALLLIGHT_RELAY_PIN, STALLLIGHT_ON);
    addLog(stallLightAutoOff
        ? "Stalllicht AN (Auto-Aus in " + String(stallLightMinutes) + " min)"
        : "Stalllicht AN (Dauerbetrieb)");
}

void stallLightOff()
{
    stallLightActive = false;
    digitalWrite(STALLLIGHT_RELAY_PIN, STALLLIGHT_OFF);  // LOW
    addLog("Stalllicht ausgeschaltet");
}

// ==================================================
// RGB TESTMODUS ROT
// ==================================================
void rgbRedOn()
{
    rgbRedActive = true;
    rgbSet(rgbRedBrightness, 0, 0);
    Serial.println("🔴 RGB Rot EIN");
    addLog("🔴 Rotlicht EIN");
}

void lightPreview(uint8_t r, uint8_t g, uint8_t b, uint8_t br, uint8_t w)
{
    rgbSetScaled(r, g, b, br, w);
}

void rgbRedSetBrightness(uint8_t b)
{
    rgbRedBrightness = b;
    if (rgbRedActive) rgbSet(b, 0, 0);
}

void rgbRedOff()
{
    rgbRedActive = false;
    rgbOff();
    Serial.println("⚫ RGB Rot AUS");
    addLog("🔴 Rotlicht AUS");
}

// ==================================================
// TIMER-HELFER
// ==================================================
void startLightForMinutes(int minutes)
{
    unsigned long addMs = (minutes <= 0) ? 10000UL : (unsigned long)minutes * 60000UL;
    unsigned long nowMs = millis();
    if (lightStateUntil > nowMs) lightStateUntil += addMs;
    else                         lightStateUntil  = nowMs + addMs;
}

void startLightForMinutesReset(int minutes)
{
    unsigned long addMs = (minutes <= 0) ? 10000UL : (unsigned long)minutes * 60000UL;
    lightStateUntil = millis() + addMs;
}

// ==================================================
// LICHT-ZUSTANDSMASCHINE
// ==================================================
void updateLightState()
{
    if (rgbRedActive) return;

    unsigned long nowMs = millis();

    // Wenn Post-Close/Post-Open Timer abgelaufen ist – auch bei manuellem Licht!
    // Verhindert dass das Licht ewig brennt wenn es vor dem Schließen manuell eingeschaltet war
    if (manualLightActive &&
        (lightState == LIGHT_POST_CLOSE || lightState == LIGHT_POST_OPEN) &&
        lightStateUntil > 0 && (long)(nowMs - lightStateUntil) >= 0)
    {
        if (lightState == LIGHT_POST_OPEN)  addLog("Locklicht nach Öffnung beendet");
        if (lightState == LIGHT_POST_CLOSE) addLog("Locklicht nach Schließen beendet");
        lightState        = LIGHT_OFF;
        manualLightActive = false;
        lightOff();
        lightActive = false;
        return;
    }

    if (manualLightActive) return;

    switch (lightState)
    {
    case LIGHT_OFF:
        if (lightActive) { lightOff(); lightActive = false; }
        break;

    case LIGHT_PRE_OPEN:
    case LIGHT_POST_OPEN:
    case LIGHT_PRE_CLOSE:
    case LIGHT_POST_CLOSE:
        if (!lightActive) { lightOn(); lightActive = true; }
        if (lightStateUntil > 0 && (long)(nowMs - lightStateUntil) >= 0)
        {
            if (lightState == LIGHT_POST_OPEN)  addLog("Locklicht nach Öffnung beendet");
            if (lightState == LIGHT_POST_CLOSE) addLog("Locklicht nach Schließen beendet");
            lightState = LIGHT_OFF;
        }
        break;
    }  // end switch
}  // end updateLightState

// ==================================================
// DIMMING UPDATE
// ==================================================
void updateDimming(unsigned long nowMs)
{
    if (!dimmingActive || !lightActive) return;
    if (nowMs >= dimStartTime && nowMs <= dimEndTime)
    {
        float    progress   = float(nowMs - dimStartTime) / float(dimEndTime - dimStartTime);
        progress            = constrain(progress, 0.0f, 1.0f);
        uint8_t  brightness = (uint8_t)constrain(int(255 * (1.0f - progress)), 0, 255);
        rgbSetScaled(rgbColorR, rgbColorG, rgbColorB, (uint8_t)((uint16_t)brightness * rgbBrightness / 255), (uint8_t)((uint16_t)rgbColorW * brightness / 255));
    }
    if (nowMs > dimEndTime) dimmingActive = false;
}

// ==================================================
// STALLLICHT AUTO-AUS
// ==================================================
void updateStallLightTimer(unsigned long nowMs)
{
    if (stallLightActive && stallLightAutoOff && stallLightOffTime > 0 && (long)(nowMs - stallLightOffTime) >= 0)
        stallLightOff();
}
