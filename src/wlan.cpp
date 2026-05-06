#include "wlan.h"
#include "logger.h"
#include "bme.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <ESPmDNS.h>
#include <Arduino.h>
#include "config.h"

extern void wdogFeed();

#define MDNS_HOSTNAME  DEVICE_HOSTNAME   // aus config.h → http://klappe.local

static unsigned long lastWifiCheck    = 0;
static unsigned long wifiBackoffMs    = 0;
static unsigned long lastReconnectTry = 0;
static bool          mdnsRunning      = false;
static bool          wifiLostLogged   = false;

// Boot-Gnadenfrist: Watchdog greift erst nach 30s ein.
// WiFi braucht manchmal 10–15s – ohne Frist würde der Hard-Reset
// die laufende Verbindung abreißen bevor sie zustande kommt.
static const unsigned long WIFI_BOOT_GRACE_MS = 30000UL;

void wifiConnectNonBlocking()
{
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(DEVICE_HOSTNAME);   // Arduino-Ebene

    // Zusätzlich über ESP-IDF API direkt setzen.
    // Notwendig damit der Router bei statischer IP den Namen findet
    // (DHCP-Hostname-Option entfällt sonst).
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif) {
        esp_netif_set_hostname(sta_netif, DEVICE_HOSTNAME);
    }

    WiFi.setSleep(false);           // Arduino-Ebene
    esp_wifi_set_ps(WIFI_PS_NONE);  // ESP-IDF Ebene – deaktiviert Modem-Sleep komplett
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);   // Manuell via wifiWatchdog() – autoReconnect kann WiFi-Stack blockieren

#ifdef STATIC_IP
    IPAddress ip(STATIC_IP);
    IPAddress gw(STATIC_GATEWAY);
    IPAddress sn(STATIC_SUBNET);
    IPAddress dns(STATIC_DNS);
    WiFi.config(ip, gw, sn, dns);
#endif

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    addLog("🔌 WLAN: Verbinde ...");
}

static void wifiHardResetStack()
{
    addLog("♻️ WLAN-Stack Neustart");
    if (bmeSource == BME_SOURCE_ESPNOW) bmeSetSource(BME_SOURCE_ESPNOW);
    wdogFeed();
    WiFi.disconnect(false);   // false = keine Flash-Löschung (blockiert sonst!)
    wdogFeed();
    WiFi.mode(WIFI_OFF);
    delay(50);
    wdogFeed();
    wifiConnectNonBlocking();
    if (bmeSource == BME_SOURCE_ESPNOW) bmeSetSource(BME_SOURCE_ESPNOW);
}

void wifiWatchdog()
{
    unsigned long nowMs = millis();

    // Während der Boot-Gnadenfrist nur prüfen ob schon verbunden – nie eingreifen
    if (nowMs < WIFI_BOOT_GRACE_MS) {
        if (WiFi.status() == WL_CONNECTED) wifiBackoffMs = 0;
        return;
    }

    if (nowMs - lastWifiCheck < 5000) return;
    lastWifiCheck = nowMs;

    if (WiFi.status() == WL_CONNECTED) {
        wifiBackoffMs = 0;
        if (wifiLostLogged) {
            addLog("📡 WLAN wiederverbunden");
            wifiLostLogged = false;
        }
        // mDNS einmal pro Verbindung starten – damit die Fritz!Box den
        // Hostnamen findet (bei statischer IP gibt es keinen DHCP-Hostname)
        if (!mdnsRunning) {
            if (MDNS.begin(MDNS_HOSTNAME)) {
                MDNS.addService("http", "tcp", 80);
                mdnsRunning = true;
                Serial.printf("✅ mDNS: %s.local\n", MDNS_HOSTNAME);
            }
        }
        return;
    }

    // WiFi verloren → mDNS muss nach Reconnect neu gestartet werden
    mdnsRunning = false;

    if (!wifiLostLogged) {
        addLog("📡 WLAN-Verbindung verloren");
        wifiLostLogged = true;
    }

    if (wifiBackoffMs == 0) wifiBackoffMs = 5000;
    else wifiBackoffMs = min(wifiBackoffMs * 2, (unsigned long)60000);

    if (nowMs - lastReconnectTry >= wifiBackoffMs)
    {
        lastReconnectTry = nowMs;
        Serial.printf("[WLAN] Reconnect-Versuch (Backoff %lus)\n", wifiBackoffMs / 1000);
        WiFi.disconnect(false, false);
        WiFi.reconnect();

        static uint8_t hardTries = 0;
        if (++hardTries >= 3) { hardTries = 0; wifiHardResetStack(); }
    }
}