#include "config.h"
#include "logger.h"
#include "storage.h"
#include "wifi_mgr.h"
#include "cli_menu.h"
#include "web_server.h"
#include "twitch_api.h"
#include "led_indicator.h"

// Global instances
AppConfig g_config;
FarmerState g_state;

// FreeRTOS Task Handle for Twitch Farmer Engine
TaskHandle_t twitchTaskHandle = NULL;

void twitchFarmerTask(void* pvParameters) {
    Logger::info("Twitch Farmer Engine Task started on Core %d", xPortGetCoreID());

    uint32_t lastInventoryCheck = 0;
    uint32_t lastHeartbeat = 0;
    uint32_t lastUserFetchAttempt = 0;
    uint32_t lastAccountRotationCheck = 0;
    uint32_t currentHeartbeatInterval = 60000;
    bool initialUserFetched = false;

    for (;;) {
        g_state.wifi_connected = WiFiManager::isConnected();

        if (g_config.farming_enabled && g_state.wifi_connected && TwitchAPI::getCleanToken().length() > 0) {
            uint32_t now = millis();

            // 1. Fetch user metadata and Client-Integrity security token on startup/connect (with 15s retry cooldown)
            if (!initialUserFetched && (lastUserFetchAttempt == 0 || (now - lastUserFetchAttempt) >= 15000)) {
                lastUserFetchAttempt = now;
                LedIndicator::setState(LED_STATE_AUTH_SEARCH);
                TwitchAPI::fetchIntegrityToken();
                vTaskDelay(pdMS_TO_TICKS(100));
                if (TwitchAPI::fetchCurrentUser()) {
                    initialUserFetched = true;
                    // Select initial game from priority queue on first successful auth
                    TwitchAPI::selectNextGameFromQueue();
                    vTaskDelay(pdMS_TO_TICKS(100));
                } else {
                    LedIndicator::setState(LED_STATE_ERROR);
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }

            if (initialUserFetched) {
                // 2. Process WebSocket PubSub events
                TwitchAPI::loopPubSub();

                // 3. Optional Multi-Account Time-Slice Auto-Rotation (e.g. every 120 minutes)
                if (g_config.account_rotation_enabled && g_config.account_count > 1) {
                    if (lastAccountRotationCheck == 0) lastAccountRotationCheck = now;
                    if ((now - lastAccountRotationCheck) >= 7200000) { // 120 min
                        Logger::info("Multi-Account Rotation: Time slice reached. Rotating to next account...");
                        lastAccountRotationCheck = millis();
                        TwitchAPI::rotateNextAccount();
                        initialUserFetched = false;
                        vTaskDelay(pdMS_TO_TICKS(500));
                        continue;
                    }
                }

                // 4. Fetch inventory & progress every 5 minutes (300,000 ms) or on initial start
                if (lastInventoryCheck == 0 || (now - lastInventoryCheck) >= 300000) {
                    TwitchAPI::fetchInventoryAndProgress();
                    vTaskDelay(pdMS_TO_TICKS(100));

                    // Only search for stream if channel/broadcast ID is not yet resolved
                    if (strlen(g_state.current_channel_id) == 0 || strlen(g_state.current_broadcast_id) == 0) {
                        TwitchAPI::findLiveStreamForGame(g_config.target_game);
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }

                    lastInventoryCheck = millis();
                }

                // 5. Send MinuteWatched heartbeat with Jitter entropy (56-68 seconds)
                now = millis();
                if (lastHeartbeat == 0 || (now - lastHeartbeat) >= currentHeartbeatInterval) {
                    if (strlen(g_state.current_channel) > 0 && strlen(g_state.current_channel_id) > 0) {
                        bool hbOk = TwitchAPI::sendMinuteWatchedHeartbeat(
                            g_state.current_channel,
                            g_state.current_channel_id,
                            g_state.current_broadcast_id
                        );
                        if (hbOk) {
                            LedIndicator::setState(LED_STATE_FARMING);
                        }
                    } else {
                        LedIndicator::setState(LED_STATE_AUTH_SEARCH);
                        TwitchAPI::findLiveStreamForGame(g_config.target_game);
                    }

                    // Compute dynamic Jitter entropy for next heartbeat interval (56 to 68 sec)
                    currentHeartbeatInterval = 56000 + (esp_random() % 12000);
                    g_state.current_heartbeat_interval_ms = currentHeartbeatInterval;
                    lastHeartbeat = millis();
                    vTaskDelay(pdMS_TO_TICKS(100));

                    // 6. Auto-rotate streamer if progress stays unchanged for 7 minutes
                    static uint16_t stuckMinutes = 0;
                    static uint8_t lastPct = 0;
                    static uint8_t stuckRotations = 0;
                    if (g_state.drop_progress_pct == lastPct && g_config.target_channel[0] == '\0') {
                        stuckMinutes++;
                        if (stuckMinutes >= 7) {
                            stuckRotations++;
                            Logger::warn("Progress unchanged for 7 min on '%s' (%u/2 rotations). Rotating streamer...", g_state.current_channel, stuckRotations);
                            TwitchAPI::markStreamerFailed(g_state.current_channel);
                            g_state.current_channel[0] = '\0';
                            g_state.current_channel_id[0] = '\0';
                            g_state.current_broadcast_id[0] = '\0';

                            if (stuckRotations >= 2) {
                                Logger::warn("Game '%s' made 0 progress after %u streamer rotations. Switching to next game in queue...", g_config.target_game, stuckRotations);
                                stuckRotations = 0;
                                stuckMinutes = 0;
                                for (uint8_t qi = 0; qi < g_config.game_queue_count; qi++) {
                                    if (strcasecmp(g_config.game_queue[qi].name, g_config.target_game) == 0) {
                                        g_config.game_queue[qi].status = GAME_QUEUED;
                                        g_config.game_queue[qi].priority += 10;
                                        break;
                                    }
                                }
                                TwitchAPI::selectNextGameFromQueue();
                            } else {
                                TwitchAPI::findLiveStreamForGame(g_config.target_game);
                                stuckMinutes = 0;
                            }
                        }
                    } else {
                        lastPct = g_state.drop_progress_pct;
                        stuckMinutes = 0;
                        stuckRotations = 0;
                    }
                }
            }
        } else {
            initialUserFetched = false;
            if (!g_state.wifi_connected) {
                LedIndicator::setState(LED_STATE_WIFI_CONNECTING);
            } else if (TwitchAPI::getCleanToken().length() == 0) {
                LedIndicator::setState(LED_STATE_AUTH_SEARCH);
            } else {
                LedIndicator::setState(LED_STATE_OFF);
            }
        }

        // Sleep task briefly (50ms) to allow PubSub WS loop responsive processing
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void setup() {
    // 1. Initialize USB CDC Serial
    Serial.begin(115200);
    delay(1000); // Give USB CDC time to attach

    // 2. Initialize Core Subsystems & LED Indicator
    Logger::init();
    Logger::info("Starting ESP32-S3 Twitch Drops Farmer v%s", FIRMWARE_VERSION);

    StorageManager::init();
    StorageManager::loadConfig(g_config);

    LedIndicator::init();
    LedIndicator::setEnabled(g_config.led_enabled);
    LedIndicator::setState(LED_STATE_WIFI_CONNECTING);

    TwitchAPI::init();
    WiFiManager::init();

    // 3. Connect to Wi-Fi or Start AP Mode
    if (strlen(g_config.wifi_ssid) > 0) {
        WiFiManager::connectSTA(g_config.wifi_ssid, g_config.wifi_pass);
    } else {
        Logger::warn("Wi-Fi SSID not configured. Launching AP Captive Portal...");
        WiFiManager::startAP();
    }

    // 4. Initialize Web Server
    WebServerManager::init();

    // 5. Print USB CLI Menu
    CLIMenu::init();

    // 6. Create Twitch Farmer Background Task (Pinned to Core 1, 32KB Stack for SSL & WebSockets)
    xTaskCreatePinnedToCore(
        twitchFarmerTask,
        "TwitchFarmer",
        32768,
        NULL,
        1,
        &twitchTaskHandle,
        1
    );

    Logger::info("System startup complete! Ready for USB or Web interactions.");
}

void loop() {
    // 1. Update Hardware RGB LED animation (33 FPS)
    LedIndicator::update();

    // 2. Process USB CDC Serial Commands
    CLIMenu::processInput();

    // 3. Process Captive Portal DNS Requests
    WiFiManager::processDNS();

    // 4. Process Web Dashboard HTTP Requests
    WebServerManager::handleClient();

    // Yield CPU time
    delay(10);
}
