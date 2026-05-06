#include "logic.h"
#include "light.h"
#include "motor.h"
#include "door.h"
#include "lux.h"
#include "storage.h"
#include "logger.h"
#include "relay.h"
#include "system.h"
#include <math.h>

// ==================================================
// GLOBALE VARIABLEN (dieses Moduls)
// ==================================================
bool          preLightForecastActive    = false;
bool          preLightForecastCondition = false;
unsigned long preLightStartedAt         = 0;

bool          closeForecastCondition    = false;
unsigned long closeForecastStableSince  = 0;

unsigned long scheduledCloseAt          = 0;
unsigned long openInterruptionSince     = 0;
unsigned long closeInterruptionSince    = 0;

bool          nightLock                 = false;
unsigned long lastDoorCloseTime         = 0;

// Lokale Hilfsvariablen
static bool          preLightHoldEnabled   = true;
static bool          preCloseHoldEnabled   = true;
static unsigned long preCloseStartedAt     = 0;
static unsigned long closeBrightTrendSince = 0;
static int           lastOpenActionMin      = -1;
static int           lastCloseActionMin     = -1;
static int           nightLockResetDay      = -1;
static unsigned long preLightForecastSince  = 0;

// Aus lux.h
extern float luxRateFiltered;
extern float lastLux;
extern unsigned long lastLuxTime;

// ==================================================
int timeToMinutes(const String &t)
{
    int sep = t.indexOf(':');
    if (sep == -1) return 0;
    return t.substring(0, sep).toInt() * 60 + t.substring(sep + 1).toInt();
}

// ==================================================
void runAutomatik(const DateTime &now, int nowMin, unsigned long nowMs,
                  bool luxValid, bool luxReady, float /*luxRateFiltered_unused*/)
{
    const bool automatikErlaubt = (manualOverrideUntil == 0 || (long)(nowMs - manualOverrideUntil) >= 0);

    static unsigned long lastDebug = 0;
    if (nowMs - lastDebug > 10000) {
        lastDebug = nowMs;
        Serial.printf("[AUTO] erlaubt=%d luxValid=%d luxReady=%d lightAuto=%d lux=%.1f closeThr=%d doorOpen=%d actionLock=%d\n",
            automatikErlaubt, luxValid, luxReady, lightAutomationAvailable, lux, closeLightThreshold, doorOpen, actionLock);
    }

    // ===== CATCH-UP nach Reboot: verpasste Aktion nachholen =====
    static bool catchUpDone = false;
    if (!catchUpDone && nowMs > 30000UL && nowMs < 600000UL)
    {
        catchUpDone = true;
        const bool inOpenWindowCatchUp  = (now.hour() >= OPEN_WINDOW_START_H  && now.hour() <= OPEN_WINDOW_END_H);
        const bool inCloseWindowCatchUp = (now.hour() >= CLOSE_WINDOW_START_H && now.hour() <= CLOSE_WINDOW_END_H);

        // Verpasste Öffnung nachholen (Zeitmodus)
        if (!doorOpen && openMode == "time" && automatikErlaubt)
        {
            int openTargetMin = timeToMinutes(openTime);
            int missedDiff = nowMin - openTargetMin;
            if (missedDiff < 0) missedDiff += 1440;
            if (missedDiff > 0 && missedDiff <= 10)
            {
                doorPhase   = PHASE_OPENING;
                motorReason = "Zeitautomatik (Catch-up nach Reboot)";
                startMotorOpen(openPosition);
                actionLock        = true;
                lastOpenActionMin = openTargetMin;
                preLightCloseDone = false;
                preLightOpenDone  = false;
                relaySendOn();
                addLog("🔁 Catch-up: Öffnung nachgeholt nach Reboot (" + String(missedDiff) + " Min verspätet)");
            }
        }
        // Verpasste Öffnung nachholen (Lichtmodus)
        // Lux ist nach 30s Boot stabil genug – Schwellwert sofort prüfen statt 30s Timer
        if (!doorOpen && openMode == "light" && automatikErlaubt && luxValid && inOpenWindowCatchUp
            && lux >= openLightThreshold && motorState == MOTOR_STOPPED && !nightLock)
        {
            doorPhase   = PHASE_OPENING;
            motorReason = "Lichtautomatik (Catch-up nach Reboot)";
            startMotorOpen(openPosition);
            actionLock        = true;
            preLightCloseDone = false;
            preLightOpenDone  = false;
            relaySendOn();
            addLog("🔁 Catch-up: Öffnung nachgeholt – Lux " + String(lux, 1) + " ≥ Schwelle " + String(openLightThreshold));
        }
        // Verpasste Schließung nachholen (Zeitmodus, nur im Schließfenster)
        if (doorOpen && closeMode == "time" && automatikErlaubt && inCloseWindowCatchUp)
        {
            int closeTargetMin = timeToMinutes(closeTime);
            int missedDiff = nowMin - closeTargetMin;
            if (missedDiff < 0) missedDiff += 1440;
            if (missedDiff > 0 && missedDiff <= 10)
            {
                doorPhase   = PHASE_CLOSING;
                motorReason = "Zeitautomatik (Catch-up nach Reboot)";
                startMotorClose(closePosition);
                lastCloseActionMin = closeTargetMin;
                addLog("🔁 Catch-up: Schließung nachgeholt nach Reboot (" + String(missedDiff) + " Min verspätet)");
                relaySendOff();
            }
        }
        // Verpasste Schließung nachholen (Lichtmodus)
        if (doorOpen && closeMode == "light" && automatikErlaubt && luxValid && inCloseWindowCatchUp
            && lux <= closeLightThreshold && motorState == MOTOR_STOPPED)
        {
            doorPhase   = PHASE_CLOSING;
            motorReason = "Lichtautomatik (Catch-up nach Reboot)";
            startMotorClose(closePosition);
            relaySendOff();
            addLog("🔁 Catch-up: Schließung nachgeholt – Lux " + String(lux, 1) + " ≤ Schwelle " + String(closeLightThreshold));
        }
    }

    // ===== SAFETY: actionLock zurücksetzen wenn Motor schon steht =====
    // Verhindert dauerhaften Automatik-Block wenn z.B. ein Web-/MQTT-Toggle
    // eintraf während die Tür bereits in der gewünschten Position war und der
    // Motor deshalb nie gestartet wurde.
    if (motorState == MOTOR_STOPPED)
        actionLock = false;

    // ===== NACHT-SPERRE RESET (morgens, einmalig) =====
    if (openMode == "light" && closeMode == "light" && nightLock &&
        now.hour() >= OPEN_WINDOW_START_H && now.hour() < CLOSE_WINDOW_START_H)
    {
        if (now.day() != nightLockResetDay)
        {
            nightLock        = false;
            nightLockResetDay = now.day();
            addLog("Nacht-Sperre zurückgesetzt");
        }
    }

    // ===== TAGES-STATISTIK RESET + VEML-NEUSTART UM 03:00 =====
    static int lastDailyResetDay = -1;
    if (now.hour() == 3 && now.minute() == 0 && now.day() != lastDailyResetDay)
    {
        lastDailyResetDay = now.day();

        // Tages-Aktionsflags zurücksetzen – sonst öffnet/schließt die Klappe
        // am nächsten Tag nicht wenn kein Neustart dazwischen war
        lastOpenActionMin  = -1;
        lastCloseActionMin = -1;

        // Statistik zurücksetzen
        if (statOpenCount > 0 || statCloseCount > 0) {
            addLog("📊 Tagesstatistik: " + String(statOpenCount) + "× geöffnet, "
                   + String(statCloseCount) + "× geschlossen, offen "
                   + String(statOpenDurationMs / 60000UL) + " min");
        }
        statOpenCount     = 0;
        statCloseCount    = 0;
        statOpenDurationMs = 0;

        // Neustart wenn VEML ausgefallen
        if (vemlHardError) {
            addLog("🔄 Automatischer Neustart – VEML7700 ausgefallen");
            loggerUpdate();  // Logs sofort sichern vor Restart
            lastRestartSource = RESTART_VEML;
            delay(500);
            ESP.restart();
        }
    }

    const bool inOpenWindow  = (now.hour() >= OPEN_WINDOW_START_H  && now.hour() <= OPEN_WINDOW_END_H);
    const bool inCloseWindow = (now.hour() >= CLOSE_WINDOW_START_H && now.hour() <= CLOSE_WINDOW_END_H);

    // ===== LUX-TREND BERECHNEN =====
    if (luxValid && lastLuxTime > 0 && nowMs - lastLuxTime >= LUX_TREND_INTERVAL_MS)
    {
        float dtMin = (nowMs - lastLuxTime) / 60000.0f;
        if (dtMin > 0.0f)
        {
            float luxRate    = (lux - lastLux) / dtMin;
            luxRateFiltered  = LUX_RATE_ALPHA * luxRate + (1.0f - LUX_RATE_ALPHA) * luxRateFiltered;
            lastLux          = lux;
            lastLuxTime      = nowMs;
        }
    }

    // ===== ZEITMODUS: VOR-LICHT ÖFFNEN =====
    if (automatikErlaubt && openMode == "time" && doorPhase == PHASE_IDLE && !doorOpen && !preLightOpenDone)
    {
        int openMin = timeToMinutes(openTime);
        int diffMin = openMin - nowMin;
        if (diffMin < 0) diffMin += 1440;
        if (diffMin > 0 && diffMin <= lampPreOpen)
        {
            lightState = LIGHT_PRE_OPEN;
            startLightForMinutesReset(lampPreOpen);
            addLog("Locklicht vor Öffnung (Zeitmodus)");
            preLightOpenDone = true;
        }
    }

    // ===== ZEITMODUS: VOR-LICHT SCHLIESSEN =====
    if (automatikErlaubt && closeMode == "time" && doorPhase == PHASE_OPEN && doorOpen && !preLightCloseDone)
    {
        int closeMin = timeToMinutes(closeTime);
        int diffMin  = closeMin - nowMin;
        if (diffMin < 0) diffMin += 1440;
        if (diffMin > 0 && diffMin <= lampPreClose)
        {
            lightState = LIGHT_PRE_CLOSE;
            startLightForMinutes(lampPreClose);
            addLog("Locklicht vor Schließen (Zeitmodus)");
            preLightCloseDone = true;
        }
    }

    // ===== LICHTMODUS: ÖFFNEN =====
    if (automatikErlaubt && luxValid && openMode == "light" && !doorOpen && !learningActive && !nightLock)
    {
        if (!inOpenWindow)
        {
            if (lightState == LIGHT_PRE_OPEN)
            {
                lightState = LIGHT_OFF;
                addLog("ℹ️ Öffnungsfenster zu – Vor-Licht beendet");
            }
            lightAboveSince    = 0;
            openInterruptionSince = 0;
        }
        else
        {
            // ---- 1) Vor-Licht per Prognose ----
            bool forecastNow = false;
            if (luxRateFiltered > MIN_POS_LUX_RATE && lux < openLightThreshold)
            {
                float minutesToThresh = (openLightThreshold - lux) / luxRateFiltered;
                if (minutesToThresh >= (float)lampPreOpen - OPEN_FORECAST_TOLERANCE_MIN &&
                    minutesToThresh <= (float)lampPreOpen + OPEN_FORECAST_TOLERANCE_MIN)
                    forecastNow = true;
            }

            if (forecastNow != preLightForecastCondition)
            {
                preLightForecastCondition = forecastNow;
                preLightForecastSince     = nowMs;
            }

            if ((nowMs - preLightForecastSince) >= PRELIGHT_MIN_STABLE_MS)
            {
                if (lux <= openLightThreshold * 0.6f)
                {
                    if (preLightForecastCondition && !preLightForecastActive)
                    {
                        lightState = LIGHT_PRE_OPEN;
                        startLightForMinutesReset(max(1, lampPreOpen));
                        addLogWithLux("Locklicht vor Öffnung gestartet (Prognose stabil)", lux);
                        preLightForecastActive = true;
                        preLightStartedAt      = nowMs;
                    }
                    if (!preLightForecastCondition && preLightForecastActive)
                    {
                        const bool safetyTimeout    = (nowMs - preLightStartedAt) >= PRELIGHT_MAX_HOLD_MS;
                        const bool thresholdNotReached = lux < openLightThreshold;
                        if (preLightHoldEnabled && thresholdNotReached && !safetyTimeout)
                        {
                            if (lightState == LIGHT_PRE_OPEN && (long)(lightStateUntil - nowMs) < 20000L)
                                startLightForMinutesReset(1);
                        }
                        else
                        {
                            lightState             = LIGHT_OFF;
                            addLog("Locklicht vor Öffnung beendet (Prognose verworfen/Timeout)");
                            preLightForecastActive = false;
                        }
                    }
                }
            }
        }

        // ---- 2) Öffnung durch Schwelle ----
        if (!actionLock && !doorOpen && lux >= openLightThreshold)
        {
            openInterruptionSince = 0;
            if (lightAboveSince == 0) lightAboveSince = nowMs;
            if (nowMs - lightAboveSince >= LIGHT_OPEN_DELAY_MS && motorState == MOTOR_STOPPED)
            {
                doorPhase   = PHASE_OPENING;
                motorReason = "Lichtautomatik";
                startMotorOpen(openPosition);
                actionLock       = true;
                preLightCloseDone = false;
                preLightOpenDone  = false;
                lightAboveSince   = 0;
                openInterruptionSince = 0;
                addLog("Öffnung gestartet (Schwellen-Erfüllung)");
                relaySendOn();
            }
        }
        else if (lux < openLightThreshold - OPEN_HYSTERESIS_LX)
        {
            if (openInterruptionSince == 0) openInterruptionSince = nowMs;
            if (nowMs - openInterruptionSince > OPEN_GLITCH_MS)
                { lightAboveSince = 0; openInterruptionSince = 0; }
        }
    }

    // ===== ZEITMODUS ÖFFNEN =====
    if (automatikErlaubt && !learningActive && !doorOpen && !nightLock)
    {
        // Zeitfenster 0 bis +1 Minute – verhindert dass Loop-Blockade die Öffnung verpasst
        // Nur NACH der eingestellten Zeit, nie davor
        int openTargetMin = timeToMinutes(openTime);
        int openDiff = nowMin - openTargetMin;
        if (openDiff < 0) openDiff += 1440;  // Mitternacht-Übergang
        if ((openMode == "time" || (openMode == "light" && !lightAutomationAvailable)) &&
            openDiff <= 1 && lastOpenActionMin != openTargetMin)
        {
            if (motorState == MOTOR_STOPPED)
            {
                doorPhase   = PHASE_OPENING;
                motorReason = (openMode == "light" && !lightAutomationAvailable) ? "Zeit-Fallback" : "Zeitautomatik";
                startMotorOpen(openPosition);
                actionLock          = true;
                lastOpenActionMin   = openTargetMin;
                preLightCloseDone   = false;
                preLightOpenDone    = false;
                relaySendOn();
                // Kein Override nötig – inCloseWindow sperrt automatisches Schließen vor 15:00
            }
        }
    }

    // ===== ZEITMODUS SCHLIESSEN =====
    if (automatikErlaubt && !learningActive && doorOpen)
    {
        // Zeitfenster 0 bis +1 Minute – nur NACH der eingestellten Zeit
        int closeTargetMin = timeToMinutes(closeTime);
        int closeDiff = nowMin - closeTargetMin;
        if (closeDiff < 0) closeDiff += 1440;
        if ((closeMode == "time" || (closeMode == "light" && !lightAutomationAvailable)) &&
            closeDiff <= 1 && lastCloseActionMin != closeTargetMin && inCloseWindow)
        {
            if (motorState == MOTOR_STOPPED)
            {
                doorPhase   = PHASE_CLOSING;
                motorReason = (closeMode == "light" && !lightAutomationAvailable) ? "Zeit-Fallback" : "Zeitautomatik";
                startMotorClose(closePosition);
                lastCloseActionMin = closeTargetMin;
                addLog("Schließvorgang gestartet (" + motorReason + ")");
                preLightOpenDone = false;
                relaySendOff();
            }
        }
    }

    // ===== SCHLIESS-PROGNOSE =====
    bool forecastNow =
        doorOpen && !manualLightActive &&
        lux > closeLightThreshold &&
        lux < closeLightThreshold + CLOSE_FORECAST_MAX_DISTANCE_LX &&
        luxRateFiltered < -MIN_NEG_LUX_RATE;

    if (forecastNow != closeForecastCondition)
        { closeForecastCondition = forecastNow; closeForecastStableSince = nowMs; }

    if (closeForecastCondition && nowMs - closeForecastStableSince > CLOSE_FORECAST_STABLE_MS)
    {
        float minutesToThresh = (lux - closeLightThreshold) / (-luxRateFiltered);
        if (!isfinite(minutesToThresh) || minutesToThresh < 0 || minutesToThresh > 180)
            minutesToThresh = 9999;

        if (doorOpen && minutesToThresh <= lampPreClose && !preLightCloseDone)
        {
            lightState = LIGHT_PRE_CLOSE;
            startLightForMinutesReset(lampPreClose);
            addLogWithLux("Locklicht vor Schließen gestartet (stabile Prognose)", lux);
            preCloseStartedAt = nowMs;
            preLightCloseDone = true;
        }
    }

    if (scheduledCloseAt != 0 && nowMs >= scheduledCloseAt && !preLightCloseDone)
    {
        if (!manualLightActive)
        {
            lightState = LIGHT_PRE_CLOSE;
            startLightForMinutesReset(max(1, lampPreClose));
            addLogWithLux("Locklicht vor Schließen gestartet (Prognose/Termin)", lux);
            preCloseStartedAt      = nowMs;
            closeBrightTrendSince  = 0;
        }
        scheduledCloseAt = 0;
    }

    // ===== PRE-CLOSE HOLD =====
    // Nur aktiv solange Klappe noch offen und Pre-Close Licht läuft
    if (preCloseHoldEnabled && lightState == LIGHT_PRE_CLOSE && doorOpen)
    {
        const bool safetyTimeout = (nowMs - preCloseStartedAt) >= PRECLOSE_MAX_HOLD_MS;

        // Während aktiver Schließverzögerung: Timer immer verlängern
        // damit das Locklicht nicht ausgeht bevor die Klappe schließt
        bool inCloseDelay = (lightBelowSince > 0 && closeDelayMin > 0);
        if (!safetyTimeout && ((long)(lightStateUntil - nowMs) < 20000L || inCloseDelay))
            startLightForMinutesReset(1);

        // ---- Abbruch-Prüfung: wird es wieder deutlich heller? ----
        const bool abortCandidate =
            (luxRateFiltered > PRECLOSE_ABORT_POS_RATE) &&
            (lux >= closeLightThreshold + PRECLOSE_ABORT_MARGIN_LX);

        if (abortCandidate)
        {
            if (closeBrightTrendSince == 0) closeBrightTrendSince = nowMs;
            if (nowMs - closeBrightTrendSince >= PRECLOSE_ABORT_STABLE_MS)
            {
                if (!preCloseHoldEnabled || safetyTimeout)
                {
                    scheduledCloseAt = 0;
                    lightState       = LIGHT_OFF;
                    addLog("Locklicht vor Schließen abgebrochen (stabil deutlich heller)");
                }
            }
        }
        else closeBrightTrendSince = 0;
    }

    // ===== SCHLIESSEN BEI SCHWELLEN-ERFÜLLUNG =====
    // FIX: inCloseWindow absichtlich NICHT geprüft.
    // Der Lux-Sensor funktioniert → wenn es dunkel genug ist, schließen wir.
    // inCloseWindow würde frühe Sonnenuntergänge im Winter (vor 15:00) blockieren
    // und ist bei funktionierendem Sensor unnötig. Der Zeit-Fallback hat sein
    // eigenes Fenster über closeTime.
    const int closeRiseThreshold = closeLightThreshold + CLOSE_HYSTERESIS_LX;

    // Schließen per Lichtsensor – erst nach 60s Uptime, wenn Sensor bereit UND im Zeitfenster
    if (!actionLock && doorOpen && motorState == MOTOR_STOPPED &&
        lightAutomationAvailable && luxReady && nowMs > 60000UL &&
        lux <= closeLightThreshold && inCloseWindow)
    {
        closeInterruptionSince = 0;
        if (lightBelowSince == 0) lightBelowSince = nowMs;

        // Gesamtverzögerung = 30s Grundverzögerung + eingestellte Minuten
        unsigned long totalDelayMs = LIGHT_DELAY_MS + (unsigned long)closeDelayMin * 60000UL;

        if (nowMs - lightBelowSince >= totalDelayMs)
        {
            doorPhase   = PHASE_CLOSING;
            motorReason = "Lichtautomatik";
            startMotorClose(closePosition);
            actionLock          = true;
            lightBelowSince     = 0;
            closeInterruptionSince = 0;
            scheduledCloseAt    = 0;
            addLog("Schließvorgang gestartet (Schwellen-Erfüllung)");
            relaySendOff();
        }
        else if (closeDelayMin > 0 && lightBelowSince > 0)
        {
            // Log-Hinweis beim Erreichen der Schwelle (nur einmal)
            static unsigned long lastDelayLog = 0;
            if (lastDelayLog != lightBelowSince) {
                lastDelayLog = lightBelowSince;
                addLogWithLux("Lux-Schwelle erreicht – Schließverzögerung " +
                    String(closeDelayMin) + " min", lux);
            }
        }
    }
    else if (lux > closeRiseThreshold)
    {
        if (closeInterruptionSince == 0) closeInterruptionSince = nowMs;
        if (nowMs - closeInterruptionSince > CLOSE_GLITCH_MS)
            { lightBelowSince = 0; closeInterruptionSince = 0; }
    }

    // Sensor ausgefallen: Timer zurücksetzen
    if (!lightAutomationAvailable)
        { lightBelowSince = 0; closeInterruptionSince = 0; }
}



// ==================================================
// AUTOMATIK KLAPPE 2 (ohne Lichtsteuerung)
// ==================================================
static int  lastDoor2OpenActionMin  = -1;
static int  lastDoor2CloseActionMin = -1;
static unsigned long door2LightAboveSince = 0;
static unsigned long door2LightBelowSince = 0;

void runAutomatik2(const DateTime &now, int nowMin, unsigned long nowMs,
                   bool luxValid, bool luxReady, float /*luxRateFiltered_unused*/)
{
    // Keine Automatik wenn ein Motor läuft (Mutual Exclusion)
    // oder Klappe 2 gerade manuell bedient wird
    if (motor2State != MOTOR_STOPPED) return;

    const bool inOpenWindow  = (now.hour() >= OPEN_WINDOW_START_H  && now.hour() <= OPEN_WINDOW_END_H);
    const bool inCloseWindow = (now.hour() >= CLOSE_WINDOW_START_H && now.hour() <= CLOSE_WINDOW_END_H);

    // ===== TAGES-RESET UM 03:00 =====
    static int lastDoor2ResetDay = -1;
    if (now.hour() == 3 && now.minute() == 0 && now.day() != lastDoor2ResetDay)
    {
        lastDoor2ResetDay = now.day();
        lastDoor2OpenActionMin  = -1;
        lastDoor2CloseActionMin = -1;
    }

    // ===== ZEITMODUS ÖFFNEN =====
    if (!door2Open && !actionLock2)
    {
        int openTargetMin = timeToMinutes(door2OpenTime);
        int openDiff = nowMin - openTargetMin;
        if (openDiff < 0) openDiff += 1440;
        if ((door2OpenMode == "time" || (door2OpenMode == "light" && !lightAutomationAvailable)) &&
            openDiff <= 1 && lastDoor2OpenActionMin != openTargetMin)
        {
            if (!isAnyMotorRunning())
            {
                door2Phase    = PHASE_OPENING;
                motor2Reason  = (door2OpenMode == "light" && !lightAutomationAvailable)
                              ? "Klappe2 Zeit-Fallback" : "Klappe2 Zeitautomatik";
                startMotor2Open(door2OpenPosition);
                actionLock2 = true;
                lastDoor2OpenActionMin = openTargetMin;
                addLog("Klappe2 Öffnung gestartet (" + motor2Reason + ")");
            }
        }
    }

    // ===== LICHTMODUS ÖFFNEN =====
    if (door2OpenMode == "light" && !door2Open && luxValid && inOpenWindow && !actionLock2)
    {
        if (lux >= door2OpenLightThreshold)
        {
            if (door2LightAboveSince == 0) door2LightAboveSince = nowMs;
            if (nowMs - door2LightAboveSince >= LIGHT_OPEN_DELAY_MS && !isAnyMotorRunning())
            {
                door2Phase    = PHASE_OPENING;
                motor2Reason  = "Klappe2 Lichtautomatik";
                startMotor2Open(door2OpenPosition);
                actionLock2          = true;
                door2LightAboveSince = 0;
                addLog("Klappe2 Öffnung (Lichtautomatik)");
            }
        }
        else if (lux < door2OpenLightThreshold - OPEN_HYSTERESIS_LX)
        {
            door2LightAboveSince = 0;
        }
    }

    // ===== ZEITMODUS SCHLIESSEN =====
    if (door2Open && !actionLock2)
    {
        int closeTargetMin = timeToMinutes(door2CloseTime);
        int closeDiff = nowMin - closeTargetMin;
        if (closeDiff < 0) closeDiff += 1440;
        if ((door2CloseMode == "time" || (door2CloseMode == "light" && !lightAutomationAvailable)) &&
            closeDiff <= 1 && lastDoor2CloseActionMin != closeTargetMin && inCloseWindow)
        {
            if (!isAnyMotorRunning())
            {
                door2Phase    = PHASE_CLOSING;
                motor2Reason  = (door2CloseMode == "light" && !lightAutomationAvailable)
                              ? "Klappe2 Zeit-Fallback" : "Klappe2 Zeitautomatik";
                startMotor2Close(door2ClosePosition);
                lastDoor2CloseActionMin = closeTargetMin;
                addLog("Klappe2 Schließvorgang (" + motor2Reason + ")");
            }
        }
    }

    // ===== LICHTMODUS SCHLIESSEN =====
    // Debug: alle 60s loggen welche Bedingungen erfüllt/nicht erfüllt sind
    static unsigned long lastD2DebugLog = 0;
    if (door2CloseMode == "light" && door2Open && nowMs - lastD2DebugLog > 60000UL)
    {
        lastD2DebugLog = nowMs;
        Serial.printf("🔍 K2-Close-Debug: lux=%.0f thresh=%d luxReady=%d lightAvail=%d actionLock=%d inCloseWin=%d below=%lu\n",
            lux, door2CloseLightThreshold, luxReady, lightAutomationAvailable, actionLock2, inCloseWindow,
            door2LightBelowSince > 0 ? (nowMs - door2LightBelowSince)/1000 : 0);
    }

    if (door2CloseMode == "light" && door2Open && !actionLock2 &&
        luxReady && nowMs > 60000UL &&
        lux <= door2CloseLightThreshold && inCloseWindow)
    {
        if (door2LightBelowSince == 0) {
            door2LightBelowSince = nowMs;
            addLog("Klappe2 Lux-Schwelle erreicht (" + String((int)lux) + " <= " + String(door2CloseLightThreshold) + ")");
        }

        unsigned long totalDelayMs = LIGHT_DELAY_MS + (unsigned long)door2CloseDelayMin * 60000UL;

        if (nowMs - door2LightBelowSince >= totalDelayMs && !isAnyMotorRunning())
        {
            door2Phase        = PHASE_CLOSING;
            motor2Reason      = "Klappe2 Lichtautomatik";
            startMotor2Close(door2ClosePosition);
            actionLock2       = true;
            door2LightBelowSince = 0;
            addLog("Klappe2 Schließvorgang (Lichtautomatik)");
        }
    }
    else if (lux > door2CloseLightThreshold + CLOSE_HYSTERESIS_LX)
    {
        door2LightBelowSince = 0;
    }

    // ===== SAFETY: actionLock2 zurücksetzen wenn Motor steht =====
    if (motor2State == MOTOR_STOPPED)
        actionLock2 = false;
}
