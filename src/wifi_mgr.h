#ifndef WIFI_MGR_H
#define WIFI_MGR_H

#include "config.h"
#include <WiFi.h>
#include <DNSServer.h>

class WiFiManager {
public:
    static void init();
    static bool connectSTA(const char* ssid, const char* pass, uint32_t timeout_ms = 20000);
    static void startAP();
    static void processDNS();
    static bool isConnected();
    static void notifyNetworkError();
    static void checkConnection();
    static String scanNetworksJson();
    static String getIP();

private:
    static DNSServer dnsServer;
    static bool isAPMode;
    static uint8_t consecutiveErrors;
    static uint32_t lastReconnectAttempt;
};

#endif // WIFI_MGR_H
