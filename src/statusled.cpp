// ==================================================
// STATUS-LED  –  WS2812 auf IO48
// ==================================================
// Bibliothek: Adafruit NeoPixel (kompatibel mit ESP32 Core 3.x)
//   → In PlatformIO: lib_deps = adafruit/Adafruit NeoPixel
//   → In Arduino IDE: Bibliothek "Adafruit NeoPixel" installieren
// ==================================================

#include "statusled.h"
#include "system.h"   // errorWifi, errorMQTT, errorSensor, otaInProgress, rtcOk
#include <Adafruit_NeoPixel.h>

// ---- Konfiguration --------------------------------
#define STATUS_LED_PIN   48    // WS2812 auf dem Board
#define STATUS_LED_COUNT  1
#define STATUS_BRIGHTNESS 30   // 0–255  (niedrig = augenschonend)

// ---- Farben (R, G, B) ----------------------------
#define COL_OFF     0,   0,   0
#define COL_GREEN   0, 255,   0
#define COL_RED   255,   0,   0
#define COL_ORANGE 255,  80,   0
#define COL_YELLOW 255, 180,   0
#define COL_BLUE    0,   0, 255

// ---- Timing (ms) ---------------------------------
#define BLINK_FAST_MS    150   // WiFi-Fehler
#define BLINK_SLOW_MS    600   // MQTT-Fehler / WiFi-Init
#define HEARTBEAT_ON_MS   80   // OK-Heartbeat Puls
#define HEARTBEAT_OFF_MS 2920  // OK-Heartbeat Pause (3 s Zyklus)
#define OTA_STEP_MS      120   // OTA-Lauflicht Schrittweite

// --------------------------------------------------
static Adafruit_NeoPixel strip(STATUS_LED_COUNT, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);

static void setColor(uint8_t r, uint8_t g, uint8_t b)
{
    strip.setPixelColor(0, strip.Color(r, g, b));
    strip.show();
}

// ---- öffentliche API -----------------------------

void statusLedInit()
{
    strip.begin();
    strip.setBrightness(STATUS_BRIGHTNESS);
    strip.clear();
    strip.show();
}

void statusLedUpdate()
{
    static unsigned long lastToggle = 0;
    static bool          ledOn      = false;
    static uint8_t       otaPhase   = 0;
    unsigned long        now        = millis();

    // --- Priorität 1: OTA ---
    if (otaInProgress) {
        statusLedOta();
        return;
    }

    // --- Priorität 2: WiFi-Fehler (schnell rot blinken) ---
    if (errorWifi) {
        if (now - lastToggle >= BLINK_FAST_MS) {
            lastToggle = now;
            ledOn = !ledOn;
            ledOn ? setColor(COL_RED) : setColor(COL_OFF);
        }
        return;
    }

    // --- Priorität 3: Sensor- oder RTC-Fehler (orange blinken) ---
    if (errorSensor || !rtcOk) {
        if (now - lastToggle >= BLINK_SLOW_MS) {
            lastToggle = now;
            ledOn = !ledOn;
            ledOn ? setColor(COL_ORANGE) : setColor(COL_OFF);
        }
        return;
    }

    // --- Priorität 4: MQTT-Fehler (langsam blau blinken) ---
    if (errorMQTT) {
        if (now - lastToggle >= BLINK_SLOW_MS) {
            lastToggle = now;
            ledOn = !ledOn;
            ledOn ? setColor(COL_BLUE) : setColor(COL_OFF);
        }
        return;
    }

    // --- Priorität 5: WiFi noch nicht verbunden (gelb langsam) ---
    if (WiFi.status() != WL_CONNECTED) {
        if (now - lastToggle >= BLINK_SLOW_MS) {
            lastToggle = now;
            ledOn = !ledOn;
            ledOn ? setColor(COL_YELLOW) : setColor(COL_OFF);
        }
        return;
    }

    // --- Priorität 6: Alles OK → kurzer grüner Herzschlag ---
    {
        unsigned long cycle = (now % (HEARTBEAT_ON_MS + HEARTBEAT_OFF_MS));
        if (cycle < HEARTBEAT_ON_MS) {
            setColor(COL_GREEN);
        } else {
            setColor(COL_OFF);
        }
    }
}

void statusLedOta()
{
    // Während OTA: LED aus, damit nichts ablenkt
    strip.clear();
    strip.show();
}