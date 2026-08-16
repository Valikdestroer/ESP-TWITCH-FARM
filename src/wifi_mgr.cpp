#include "wifi_mgr.h"
#include "logger.h"
#include <ArduinoJson.h>

DNSServer WiFiManager::dnsServer;
bool WiFiManager::isAPMode = false;
uint8_t WiFiManager::consecutiveErrors = 0;
uint32_t WiFiManager::lastReconnectAttempt = 0;

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

    // Fallback Anycast DNS servers: Cloudflare (1.1.1.1), Google (8.8.8.8)
    IPAddress primaryDNS(1, 1, 1, 1);
    IPAddress secondaryDNS(8, 8, 8, 8);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, primaryDNS, secondaryDNS);

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
        consecutiveErrors = 0;
        String ipStr = WiFi.localIP().toString();
        strncpy(g_state.current_ip, ipStr.c_str(), sizeof(g_state.current_ip));
        g_state.wifi_connected = true;

        // Synchronize UTC time via NTP (multiple fallback servers)
        configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");

        Logger::info("Wi-Fi Connected! IP Address: %s (RSSI: %d dBm, DNS: 1.1.1.1)", ipStr.c_str(), WiFi.RSSI());
        return true;
    } else {
        Logger::error("Wi-Fi connection failed. Starting Access Point mode...");
        g_state.wifi_connected = false;
        startAP();
        return false;
    }
}

void WiFiManager::notifyNetworkError() {
    consecutiveErrors++;
    if (consecutiveErrors >= 3 && !isAPMode) {
        uint32_t now = millis();
        if (now - lastReconnectAttempt > 30000) {
            lastReconnectAttempt = now;
            Logger::warn("Network/DNS errors threshold reached (%u). Triggering Wi-Fi soft-reconnect...", consecutiveErrors);
            WiFi.reconnect();
        }
    }
}

void WiFiManager::checkConnection() {
    if (!isAPMode && WiFi.status() == WL_CONNECTED) {
        consecutiveErrors = 0;
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
