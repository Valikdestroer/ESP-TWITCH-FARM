#include "wifi_mgr.h"
#include "logger.h"
#include <ArduinoJson.h>

DNSServer WiFiManager::dnsServer;
bool WiFiManager::isAPMode = false;

void WiFiManager::init() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
}

bool WiFiManager::connectSTA(const char* ssid, const char* pass, uint32_t timeout_ms) {
    if (strlen(ssid) == 0) {
        Logger::warn("Wi-Fi SSID is empty. Switching to AP Mode...");
        startAP();
        return false;
    }

    Logger::info("Connecting to Wi-Fi SSID: %s ...", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < timeout_ms) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        if (isAPMode) {
            dnsServer.stop();
        }
        isAPMode = false;
        String ipStr = WiFi.localIP().toString();
        strncpy(g_state.current_ip, ipStr.c_str(), sizeof(g_state.current_ip));
        g_state.wifi_connected = true;

        // Synchronize UTC time via NTP
        configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");

        Logger::info("Wi-Fi Connected! IP Address: %s (RSSI: %d dBm)", ipStr.c_str(), WiFi.RSSI());
        return true;
    } else {
        Logger::error("Wi-Fi connection failed. Starting Access Point mode...");
        g_state.wifi_connected = false;
        startAP();
        return false;
    }
}

void WiFiManager::startAP() {
    isAPMode = true;
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(DEFAULT_AP_SSID, DEFAULT_AP_PASS);
    
    IPAddress apIP = WiFi.softAPIP();
    String ipStr = apIP.toString();
    strncpy(g_state.current_ip, ipStr.c_str(), sizeof(g_state.current_ip));
    g_state.wifi_connected = false;

    // Captive Portal DNS server on port 53
    dnsServer.start(53, "*", apIP);
    Logger::info("AP Mode Active. Connect to SSID: '%s' (Pass: '%s'), open http://%s", DEFAULT_AP_SSID, DEFAULT_AP_PASS, ipStr.c_str());
}

void WiFiManager::processDNS() {
    if (isAPMode) {
        dnsServer.processNextRequest();
    }
}

bool WiFiManager::isConnected() {
    return (WiFi.status() == WL_CONNECTED);
}

String WiFiManager::scanNetworksJson() {
    Logger::info("Scanning nearby Wi-Fi networks...");
    int n = WiFi.scanNetworks();
    
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (int i = 0; i < n; ++i) {
        JsonObject net = arr.add<JsonObject>();
        net["ssid"] = WiFi.SSID(i);
        net["rssi"] = WiFi.RSSI(i);
        net["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }
    WiFi.scanDelete();

    String output;
    serializeJson(doc, output);
    return output;
}

String WiFiManager::getIP() {
    if (isAPMode) {
        return WiFi.softAPIP().toString();
    }
    return WiFi.localIP().toString();
}
