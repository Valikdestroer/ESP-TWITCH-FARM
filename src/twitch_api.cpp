#include "twitch_api.h"
#include "logger.h"
#include "storage.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>

extern "C" {
#include <rom/miniz.h>
}
#include <esp_rom_crc.h>

const char* GQL_ENDPOINT = "https://gql.twitch.tv/gql";
static WebSocketsClient wsClient;
static uint32_t lastPubSubPingTime = 0;
static char currentSubscribedChannelId[32] = "";
static char g_session_id[64] = "";
static char g_integrity_token[512] = "";

// Failed/offline streamer blacklist to avoid stuck loops
#define MAX_FAILED_STREAMERS 16
static char s_failed_streamers[MAX_FAILED_STREAMERS][64];
static uint32_t s_failed_streamer_time[MAX_FAILED_STREAMERS];
static uint8_t s_failed_streamer_count = 0;

void TwitchAPI::markStreamerFailed(const char* channelLogin) {
    if (!channelLogin || !channelLogin[0]) return;
    uint32_t now = millis();
    for (uint8_t i = 0; i < s_failed_streamer_count; i++) {
        if (strcasecmp(s_failed_streamers[i], channelLogin) == 0) {
            s_failed_streamer_time[i] = now;
            return;
        }
    }
    if (s_failed_streamer_count < MAX_FAILED_STREAMERS) {
        strncpy(s_failed_streamers[s_failed_streamer_count], channelLogin, sizeof(s_failed_streamers[0]) - 1);
        s_failed_streamers[s_failed_streamer_count][sizeof(s_failed_streamers[0]) - 1] = '\0';
        s_failed_streamer_time[s_failed_streamer_count] = now;
        s_failed_streamer_count++;
    } else {
        uint8_t oldestIdx = 0;
        uint32_t oldestTime = s_failed_streamer_time[0];
        for (uint8_t i = 1; i < MAX_FAILED_STREAMERS; i++) {
            if (s_failed_streamer_time[i] < oldestTime) {
                oldestTime = s_failed_streamer_time[i];
                oldestIdx = i;
            }
        }
        strncpy(s_failed_streamers[oldestIdx], channelLogin, sizeof(s_failed_streamers[0]) - 1);
        s_failed_streamers[oldestIdx][sizeof(s_failed_streamers[0]) - 1] = '\0';
        s_failed_streamer_time[oldestIdx] = now;
    }
    Logger::info("Streamer '%s' added to cooldown blacklist (20m).", channelLogin);
}

bool TwitchAPI::isStreamerFailed(const char* channelLogin) {
    if (!channelLogin || !channelLogin[0]) return false;
    uint32_t now = millis();
    for (uint8_t i = 0; i < s_failed_streamer_count; i++) {
        if (strcasecmp(s_failed_streamers[i], channelLogin) == 0) {
            if (now - s_failed_streamer_time[i] < 1200000) { // 20 minute cooldown
                return true;
            }
        }
    }
    return false;
}

// Drop claim attempt tracking to prevent duplicate claim spam
#define MAX_ATTEMPTED_CLAIMS 16
static char s_attemptedClaims[MAX_ATTEMPTED_CLAIMS][128];
static uint32_t s_attemptedClaimTime[MAX_ATTEMPTED_CLAIMS];
static uint8_t s_attemptedClaimCount = 0;

static bool isClaimAttemptedRecently(const char* instId) {
    if (!instId || !instId[0]) return true;
    uint32_t now = millis();
    for (uint8_t i = 0; i < s_attemptedClaimCount; i++) {
        if (strcmp(s_attemptedClaims[i], instId) == 0) {
            if (now - s_attemptedClaimTime[i] < 1800000) { // 30 minute cooldown
                return true;
            }
        }
    }
    return false;
}

static void markClaimAttempted(const char* instId) {
    if (!instId || !instId[0]) return;
    uint32_t now = millis();
    for (uint8_t i = 0; i < s_attemptedClaimCount; i++) {
        if (strcmp(s_attemptedClaims[i], instId) == 0) {
            s_attemptedClaimTime[i] = now;
            return;
        }
    }
    if (s_attemptedClaimCount < MAX_ATTEMPTED_CLAIMS) {
        strncpy(s_attemptedClaims[s_attemptedClaimCount], instId, sizeof(s_attemptedClaims[0]) - 1);
        s_attemptedClaims[s_attemptedClaimCount][sizeof(s_attemptedClaims[0]) - 1] = '\0';
        s_attemptedClaimTime[s_attemptedClaimCount] = now;
        s_attemptedClaimCount++;
    } else {
        uint8_t oldest = 0;
        for (uint8_t i = 1; i < MAX_ATTEMPTED_CLAIMS; i++) {
            if (s_attemptedClaimTime[i] < s_attemptedClaimTime[oldest]) oldest = i;
        }
        strncpy(s_attemptedClaims[oldest], instId, sizeof(s_attemptedClaims[0]) - 1);
        s_attemptedClaims[oldest][sizeof(s_attemptedClaims[0]) - 1] = '\0';
        s_attemptedClaimTime[oldest] = now;
    }
}

String TwitchAPI::getCleanToken() {
    String token = g_config.oauth_token;
    token.trim();
    if (token.startsWith("oauth:")) {
        token = token.substring(6);
    }
    return token;
}

const char* TwitchAPI::getClientId() {
    return DEFAULT_CLIENT_ID;
}

void TwitchAPI::init() {
    uint64_t mac = ESP.getEfuseMac();
    uint32_t macLow = (uint32_t)mac;
    uint32_t macHigh = (uint32_t)(mac >> 32);
    snprintf(g_state.device_id, sizeof(g_state.device_id), "%08x%08x%08x%08x", macLow, macHigh, macLow ^ 0xDEADBEEF, macHigh ^ 0xCAFEBABE);
    Logger::info("Twitch API Client initialized. Device ID: %s | Client-ID: %s", g_state.device_id, getClientId());
}

// ---------------------------------------------------------------------------
// Priority Game Queue: Select next game to farm
// ---------------------------------------------------------------------------
void TwitchAPI::selectNextGameFromQueue() {
    if (g_config.game_queue_count == 0) {
        Logger::info("Game queue is empty. Switching to auto-discovery mode...");
        g_config.target_game[0] = '\0';
        autoDiscoverCampaigns();
        return;
    }

    // Sort queue by priority (simple insertion sort, max 8 elements)
    for (uint8_t i = 1; i < g_config.game_queue_count; i++) {
        GameEntry temp = g_config.game_queue[i];
        int j = i - 1;
        while (j >= 0 && g_config.game_queue[j].priority > temp.priority) {
            g_config.game_queue[j + 1] = g_config.game_queue[j];
            j--;
        }
        g_config.game_queue[j + 1] = temp;
    }

    // Find first game that is not COMPLETED
    bool found = false;
    for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
        GameEntry& entry = g_config.game_queue[i];
        if (entry.status != GAME_COMPLETED) {
            snprintf(g_config.target_game, sizeof(g_config.target_game), "%s", entry.name);
            entry.status = GAME_FARMING;
            found = true;
            Logger::info("Queue: Now farming '%s' (Priority #%d, Progress: %d%%)", entry.name, entry.priority, entry.progress_pct);

            // Clear current streamer to force re-search for the new game
            g_state.current_channel[0] = '\0';
            g_state.current_channel_id[0] = '\0';
            g_state.current_broadcast_id[0] = '\0';
            break;
        }
    }

    if (!found) {
        Logger::info("All priority games completed! Triggering auto-discovery...");
        autoDiscoverCampaigns();

        // Try again after discovery
        for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
            if (g_config.game_queue[i].status != GAME_COMPLETED) {
                snprintf(g_config.target_game, sizeof(g_config.target_game), "%s", g_config.game_queue[i].name);
                g_config.game_queue[i].status = GAME_FARMING;
                Logger::info("Queue: Auto-farming discovered game '%s'", g_config.game_queue[i].name);
                g_state.current_channel[0] = '\0';
                g_state.current_channel_id[0] = '\0';
                g_state.current_broadcast_id[0] = '\0';
                found = true;
                break;
            }
        }

        if (!found) {
            Logger::info("No more drop campaigns available. Idling...");
            g_config.target_game[0] = '\0';
        }
    }

    StorageManager::saveConfig(g_config);
}

// ---------------------------------------------------------------------------
// Auto-discover active campaigns from Twitch profile
// ---------------------------------------------------------------------------
void TwitchAPI::autoDiscoverCampaigns() {
    Logger::info("Auto-discovering campaigns from Twitch profile...");

    String payload = "[{\"operationName\":\"Inventory\",\"query\":\"query Inventory { currentUser { inventory { dropCampaignsInProgress { id name status game { id name } timeBasedDrops { id name requiredMinutesWatched self { currentMinutesWatched dropInstanceID isClaimed } } } } } }\",\"variables\":{}}]";

    String response = sendGraphQLRequest(payload);
    if (response.length() == 0) {
        Logger::warn("Auto-discovery failed: empty GQL response");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, response)) {
        Logger::warn("Auto-discovery failed: JSON parse error");
        return;
    }

    JsonArray campaigns = doc[0]["data"]["currentUser"]["inventory"]["dropCampaignsInProgress"].as<JsonArray>();
    if (campaigns.isNull() || campaigns.size() == 0) {
        // Try ViewerDropsDashboard as fallback
        String dashPayload = "[{\"operationName\":\"ViewerDropsDashboard\",\"query\":\"query ViewerDropsDashboard { currentUser { dropCampaigns { id name status game { id name } timeBasedDrops { id name requiredMinutesWatched self { currentMinutesWatched dropInstanceID isClaimed } } } } }\",\"variables\":{}}]";
        response = sendGraphQLRequest(dashPayload);
        if (response.length() == 0) return;

        JsonDocument dashDoc;
        if (deserializeJson(dashDoc, response)) return;
        campaigns = dashDoc[0]["data"]["currentUser"]["dropCampaigns"].as<JsonArray>();
    }

    if (campaigns.isNull()) {
        Logger::warn("No campaigns found in Twitch profile for auto-discovery.");
        return;
    }

    uint8_t added = 0;
    for (JsonObject c : campaigns) {
        if (g_config.game_queue_count >= MAX_PRIORITY_GAMES) break;

        const char* gName = c["game"]["name"] | "";
        const char* cStatus = c["status"] | "";
        if (strlen(gName) == 0 || strcmp(cStatus, "EXPIRED") == 0) continue;

        // Check if this game is already in the queue
        bool alreadyExists = false;
        for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
            if (strcasestr(g_config.game_queue[i].name, gName) != NULL ||
                strcasestr(gName, g_config.game_queue[i].name) != NULL) {
                alreadyExists = true;
                break;
            }
        }

        if (!alreadyExists) {
            GameEntry& entry = g_config.game_queue[g_config.game_queue_count];
            strncpy(entry.name, gName, sizeof(entry.name) - 1);
            entry.name[sizeof(entry.name) - 1] = '\0';
            entry.priority = 200 + g_config.game_queue_count; // Low priority
            entry.status = GAME_AUTO_DISCOVERED;
            entry.progress_pct = 0;
            entry.minutes_watched = 0;

            // Extract progress from campaign data
            JsonArray drops = c["timeBasedDrops"].as<JsonArray>();
            for (JsonObject d : drops) {
                int reqWatch = d["requiredMinutesWatched"] | 60;
                int curWatch = d["self"]["currentMinutesWatched"] | 0;
                if (reqWatch > 0) {
                    entry.progress_pct = (uint8_t)((float)curWatch * 100.0f / (float)reqWatch + 0.5f);
                    entry.minutes_watched = curWatch;
                }
            }

            g_config.game_queue_count++;
            added++;
            Logger::info("Auto-discovered campaign: '%s' (Progress: %d%%)", gName, entry.progress_pct);
        }
    }

    if (added > 0) {
        Logger::info("Auto-discovered %d new campaigns from Twitch profile", added);
        StorageManager::saveConfig(g_config);
    } else {
        Logger::info("No new campaigns to discover — all are already in queue.");
    }
}


bool TwitchAPI::fetchIntegrityToken() {
    String token = getCleanToken();
    if (token.length() == 0) return false;

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, "https://gql.twitch.tv/integrity");
    http.setTimeout(8000);

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Client-ID", getClientId());
    
    String authHeader = "OAuth ";
    authHeader += token;
    http.addHeader("Authorization", authHeader);
    http.addHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36");
    http.addHeader("X-Device-Id", g_state.device_id);

    int httpCode = http.POST("{}");
    if (httpCode == HTTP_CODE_OK) {
        String response = http.getString();
        JsonDocument doc;
        if (!deserializeJson(doc, response)) {
            const char* itoken = doc["token"] | "";
            if (strlen(itoken) > 0) {
                strncpy(g_integrity_token, itoken, sizeof(g_integrity_token));
                Logger::info("Acquired Twitch Client-Integrity Token!");
                http.end();
                return true;
            }
        }
    }
    Logger::warn("Client-Integrity Token fetch status: HTTP %d", httpCode);
    http.end();
    return false;
}

String TwitchAPI::sendGraphQLRequest(const String& payload) {
    String token = getCleanToken();
    if (token.length() == 0) {
        Logger::warn("OAuth token not configured!");
        return "";
    }

    static String lastLoggedToken = "";
    if (token != lastLoggedToken) {
        lastLoggedToken = token;
        String prefix = token.length() >= 4 ? token.substring(0, 4) : token;
        String suffix = token.length() >= 8 ? token.substring(token.length() - 4) : "";
        Logger::info("Using OAuth token (len=%d, mask: %s...%s)", token.length(), prefix.c_str(), suffix.c_str());
    }

    if (strlen(g_integrity_token) == 0) {
        fetchIntegrityToken();
    }

    WiFiClientSecure client;
    client.setInsecure(); // Skip certificate validation for performance on ESP32

    auto executePost = [&](bool includeIntegrity, const char* authPrefix) -> std::pair<int, String> {
        HTTPClient http;
        http.begin(client, GQL_ENDPOINT);
        http.setTimeout(10000);

        http.addHeader("Content-Type", "application/json");
        http.addHeader("Client-ID", getClientId());
        
        String authHeader = authPrefix;
        authHeader += token;
        http.addHeader("Authorization", authHeader);
        http.addHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36");
        http.addHeader("X-Device-Id", g_state.device_id);

        if (g_session_id[0] != '\0') {
            http.addHeader("Client-Session-Id", g_session_id);
        }
        char reqId[40];
        snprintf(reqId, sizeof(reqId), "%08x%08x%08x%08x", esp_random(), esp_random(), esp_random(), esp_random());
        http.addHeader("Client-Request-Id", reqId);

        if (includeIntegrity && strlen(g_integrity_token) > 0) {
            http.addHeader("Client-Integrity", g_integrity_token);
        }

        int code = http.POST(payload);
        String resp = http.getString();
        http.end();
        return {code, resp};
    };

    auto res = executePost(true, "OAuth ");
    if (res.first == HTTP_CODE_OK) {
        return res.second;
    }

    // Fallback 1: Retry without Client-Integrity if primary request failed
    if (res.first != HTTP_CODE_OK && strlen(g_integrity_token) > 0) {
        g_integrity_token[0] = '\0';
        res = executePost(false, "OAuth ");
        if (res.first == HTTP_CODE_OK) {
            return res.second;
        }
    }

    // Fallback 2: Try Bearer prefix instead of OAuth prefix
    if (res.first != HTTP_CODE_OK) {
        res = executePost(false, "Bearer ");
        if (res.first == HTTP_CODE_OK) {
            return res.second;
        }
    }

    Logger::error("GraphQL POST failed, HTTP Code: %d, Response: %s", res.first, res.second.c_str());
    return "";
}

bool TwitchAPI::fetchCurrentUser() {
    Logger::info("Fetching authenticated Twitch user info...");
    String payload = "[{\"operationName\":\"CurrentUser\",\"query\":\"query CurrentUser { currentUser { id login displayName } }\",\"variables\":{}}]";
    
    String response = sendGraphQLRequest(payload);
    if (response.length() == 0) return false;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        Logger::error("JSON parse error in CurrentUser: %s", error.c_str());
        return false;
    }

    JsonArray root = doc.as<JsonArray>();
    if (root.size() > 0 && root[0]["data"]["currentUser"].is<JsonObject>()) {
        JsonObject user = root[0]["data"]["currentUser"];
        const char* uid = user["id"] | "";
        const char* login = user["login"] | "";
        strncpy(g_state.user_id, uid, sizeof(g_state.user_id));
        strncpy(g_state.user_login, login, sizeof(g_state.user_login));
        Logger::info("Authenticated as Twitch user: %s (ID: %s)", g_state.user_login, g_state.user_id);
        return true;
    }
    Logger::warn("Failed to retrieve current user info (invalid token?).");
    return false;
}

bool TwitchAPI::fetchInventoryAndProgress() {
    Logger::info("Fetching Drops inventory & progress...");

    String payload = "[{\"operationName\":\"Inventory\",\"query\":\"query Inventory { currentUser { inventory { dropCampaignsInProgress { id name status game { id name } timeBasedDrops { id name requiredMinutesWatched self { currentMinutesWatched dropInstanceID isClaimed } } } } } }\",\"variables\":{}}]";

    String response = sendGraphQLRequest(payload);
    if (response.length() == 0) return false;

    Logger::info("GQL Inventory Response: %s", response.length() > 300 ? response.substring(0, 300).c_str() : response.c_str());

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        Logger::error("JSON parse error in Inventory: %s", error.c_str());
        return false;
    }

    JsonArray root = doc.as<JsonArray>();
    if (root.size() > 0 && root[0]["data"].is<JsonObject>()) {
        JsonObject dataObj = root[0]["data"];
        if (dataObj["currentUser"].isNull()) {
            Logger::warn("Twitch GQL returned currentUser: null. OAuth token might be invalid!");
            return false;
        }

        if (dataObj["currentUser"]["inventory"].is<JsonObject>()) {
            JsonArray campaigns = dataObj["currentUser"]["inventory"]["dropCampaignsInProgress"].as<JsonArray>();
            if (campaigns.size() > 0) {
                // Auto-discover: add active campaign games to priority queue
                bool queueModified = false;
                for (JsonObject camp : campaigns) {
                    if (g_config.game_queue_count >= MAX_PRIORITY_GAMES) break;
                    const char* campGameName = camp["game"]["name"] | "";
                    const char* campStatus = camp["status"] | "";
                    if (strlen(campGameName) == 0 || strcmp(campStatus, "EXPIRED") == 0) continue;

                    bool exists = false;
                    for (uint8_t qi = 0; qi < g_config.game_queue_count; qi++) {
                        if (strcasestr(g_config.game_queue[qi].name, campGameName) != NULL ||
                            strcasestr(campGameName, g_config.game_queue[qi].name) != NULL) {
                            exists = true;
                            break;
                        }
                    }

                    if (!exists) {
                        GameEntry& entry = g_config.game_queue[g_config.game_queue_count];
                        strncpy(entry.name, campGameName, sizeof(entry.name) - 1);
                        entry.name[sizeof(entry.name) - 1] = '\0';
                        entry.priority = 100 + g_config.game_queue_count;
                        entry.status = GAME_AUTO_DISCOVERED;
                        entry.progress_pct = 0;
                        entry.minutes_watched = 0;

                        JsonArray campDrops = camp["timeBasedDrops"].as<JsonArray>();
                        for (JsonObject cd : campDrops) {
                            int req = cd["requiredMinutesWatched"] | 60;
                            int cur = cd["self"]["currentMinutesWatched"] | 0;
                            if (req > 0) {
                                entry.progress_pct = (uint8_t)((float)cur * 100.0f / (float)req + 0.5f);
                                entry.minutes_watched = cur;
                            }
                        }

                        g_config.game_queue_count++;
                        queueModified = true;
                        Logger::info("Auto-queued active campaign: '%s' (Progress: %d%%)", campGameName, entry.progress_pct);
                    }
                }
                if (queueModified) StorageManager::saveConfig(g_config);                // 1. Process ALL active campaigns in inventory: claim drops and update queue progress
                for (JsonObject camp : campaigns) {
                    const char* campGameName = camp["game"]["name"] | "";
                    const char* campStatus = camp["status"] | "";
                    if (strcmp(campStatus, "EXPIRED") == 0 || strlen(campGameName) == 0) continue;

                    int cMaxCurWatch = 0;
                    int cMaxReqWatch = 60;

                    JsonArray campDrops = camp["timeBasedDrops"].as<JsonArray>();
                    for (JsonObject d : campDrops) {
                        int reqWatch = d["requiredMinutesWatched"] | 60;
                        JsonObject selfObj = d["self"];
                        int curWatch = selfObj["currentMinutesWatched"] | 0;
                        bool isClaimed = selfObj["isClaimed"] | false;
                        const char* instanceId = selfObj["dropInstanceID"] | "";

                        if (curWatch > cMaxCurWatch) cMaxCurWatch = curWatch;
                        if (reqWatch > 0) cMaxReqWatch = reqWatch;

                        // Check & claim completed drops across ANY campaign
                        if (!isClaimed && reqWatch > 0 && curWatch >= reqWatch && strlen(instanceId) > 0) {
                            if (!isClaimAttemptedRecently(instanceId)) {
                                markClaimAttempted(instanceId);
                                Logger::warn("🎉 DROP READY TO CLAIM! Instance ID: %s", instanceId);
                                if (g_config.auto_claim) {
                                    claimDrop(instanceId);
                                }
                            }
                        }
                    }

                    uint8_t cPct = (cMaxReqWatch > 0) ? (uint8_t)((float)cMaxCurWatch * 100.0f / (float)cMaxReqWatch + 0.5f) : 0;

                    // Update corresponding queue entry with progress
                    for (uint8_t qi = 0; qi < g_config.game_queue_count; qi++) {
                        if (strcasestr(campGameName, g_config.game_queue[qi].name) != NULL ||
                            strcasestr(g_config.game_queue[qi].name, campGameName) != NULL) {
                            if (cPct > g_config.game_queue[qi].progress_pct) {
                                g_config.game_queue[qi].progress_pct = cPct;
                                g_config.game_queue[qi].minutes_watched = cMaxCurWatch;
                            }
                            if (cPct >= 100 && g_config.game_queue[qi].status == GAME_FARMING) {
                                g_config.game_queue[qi].status = GAME_COMPLETED;
                                Logger::info("Game '%s' completed! Moving to next in queue...", g_config.game_queue[qi].name);
                                StorageManager::saveConfig(g_config);
                                selectNextGameFromQueue();
                            }
                            break;
                        }
                    }
                }

                // 2. Find campaign for the active target_game
                JsonObject selectedCampaign;
                bool foundActive = false;

                for (JsonObject c : campaigns) {
                    const char* gName = c["game"]["name"] | "";
                    const char* cStatus = c["status"] | "";
                    if (strcmp(cStatus, "EXPIRED") != 0 && strlen(g_config.target_game) > 0 && 
                        (strcasestr(gName, g_config.target_game) != NULL || strcasestr(g_config.target_game, gName) != NULL)) {
                        selectedCampaign = c;
                        foundActive = true;
                        break;
                    }
                }

                // 3. Fallback only if NO target_game is set: pick the first active campaign
                if (!foundActive && strlen(g_config.target_game) == 0) {
                    for (JsonObject c : campaigns) {
                        const char* cStatus = c["status"] | "";
                        if (strcmp(cStatus, "EXPIRED") != 0) {
                            selectedCampaign = c;
                            foundActive = true;
                            const char* newGame = c["game"]["name"] | "";
                            if (strlen(newGame) > 0) {
                                Logger::info("Auto-selected target game '%s' from active inventory", newGame);
                                snprintf(g_config.target_game, sizeof(g_config.target_game), "%s", newGame);
                                g_state.current_channel[0] = '\0';
                                g_state.current_channel_id[0] = '\0';
                                g_state.current_broadcast_id[0] = '\0';
                                StorageManager::saveConfig(g_config);
                            }
                            break;
                        }
                    }
                }

                if (foundActive) {
                    const char* cName = selectedCampaign["name"] | "Active Drop";
                    const char* gName = selectedCampaign["game"]["name"] | g_config.target_game;
                    const char* gId = selectedCampaign["game"]["id"] | "500188";
                    
                    int maxCurWatch = 0;
                    int maxReqWatch = 60;

                    JsonArray drops = selectedCampaign["timeBasedDrops"].as<JsonArray>();
                    for (JsonObject d : drops) {
                        int reqWatch = d["requiredMinutesWatched"] | 60;
                        JsonObject selfObj = d["self"];
                        int curWatch = selfObj["currentMinutesWatched"] | 0;
                        bool isClaimed = selfObj["isClaimed"] | false;
                        const char* instanceId = selfObj["dropInstanceID"] | "";

                        if (curWatch > maxCurWatch) maxCurWatch = curWatch;
                        if (reqWatch > 0) maxReqWatch = reqWatch;

                        if (strlen(instanceId) > 0 && !isClaimed) {
                            snprintf(g_state.drop_instance_id, sizeof(g_state.drop_instance_id), "%s", instanceId);
                        }
                    }

                    uint8_t pct = (maxReqWatch > 0) ? (uint8_t)((float)maxCurWatch * 100.0f / (float)maxReqWatch + 0.5f) : 0;
                    snprintf(g_state.active_drop_name, sizeof(g_state.active_drop_name), "%s", cName);
                    snprintf(g_state.current_game, sizeof(g_state.current_game), "%s", gName);
                    snprintf(g_state.current_game_id, sizeof(g_state.current_game_id), "%s", gId);
                    g_state.drop_progress_pct = pct;

                    Logger::info("Twitch Campaign: '%s' (%s) — Progress: %d%% (%d/%d min)", cName, gName, pct, maxCurWatch, maxReqWatch);
                    return true;
                }
            }
        }
    }

    // Fallback: If dropCampaignsInProgress is empty, query ViewerDropsDashboard with exact schema
    String dashPayload = "[{\"operationName\":\"ViewerDropsDashboard\",\"query\":\"query ViewerDropsDashboard { currentUser { dropCampaigns { id name status game { id name } timeBasedDrops { id name requiredMinutesWatched self { currentMinutesWatched dropInstanceID isClaimed } } } } }\",\"variables\":{}}]";
    String dashResp = sendGraphQLRequest(dashPayload);
    if (dashResp.length() > 0) {
        Logger::info("GQL DropsDashboard Response: %s", dashResp.length() > 300 ? dashResp.substring(0, 300).c_str() : dashResp.c_str());
        JsonDocument dashDoc;
        if (!deserializeJson(dashDoc, dashResp)) {
            JsonArray avail = dashDoc[0]["data"]["currentUser"]["dropCampaigns"].as<JsonArray>();
            if (avail.size() > 0) {
                Logger::info("Discovered %d Drop Campaigns on Twitch:", avail.size());
                for (JsonObject c : avail) {
                    const char* cName = c["name"] | "";
                    const char* gName = c["game"]["name"] | "";
                    const char* gId = c["game"]["id"] | "500188";
                    const char* cStatus = c["status"] | "";

                    int maxCurWatch = 0;
                    int maxReqWatch = 60;
                    JsonArray drops = c["timeBasedDrops"].as<JsonArray>();
                    for (JsonObject d : drops) {
                        int reqWatch = d["requiredMinutesWatched"] | 60;
                        int curWatch = d["self"]["currentMinutesWatched"] | 0;
                        if (curWatch > maxCurWatch) maxCurWatch = curWatch;
                        if (reqWatch > 0) maxReqWatch = reqWatch;
                    }
                    uint8_t pct = (maxReqWatch > 0) ? (uint8_t)((float)maxCurWatch * 100.0f / (float)maxReqWatch + 0.5f) : 0;
                    Logger::info(" - Campaign: '%s' | Game: '%s' | Status: %s | Progress: %d%% (%d/%d min)", cName, gName, cStatus, pct, maxCurWatch, maxReqWatch);

                    if (strlen(g_config.target_game) > 0 && strcasestr(gName, g_config.target_game) != NULL) {
                        snprintf(g_state.active_drop_name, sizeof(g_state.active_drop_name), "%s", cName);
                        snprintf(g_state.current_game, sizeof(g_state.current_game), "%s", gName);
                        snprintf(g_state.current_game_id, sizeof(g_state.current_game_id), "%s", gId);
                        g_state.drop_progress_pct = pct;
                    }

                    // Auto-add account-linked campaign games to queue
                    if (strcmp(cStatus, "EXPIRED") != 0 && strlen(gName) > 0 && g_config.game_queue_count < MAX_PRIORITY_GAMES) {
                        bool exists = false;
                        for (uint8_t qi = 0; qi < g_config.game_queue_count; qi++) {
                            if (strcasestr(g_config.game_queue[qi].name, gName) != NULL ||
                                strcasestr(gName, g_config.game_queue[qi].name) != NULL) {
                                exists = true;
                                break;
                            }
                        }
                        if (!exists) {
                            GameEntry& entry = g_config.game_queue[g_config.game_queue_count];
                            strncpy(entry.name, gName, sizeof(entry.name) - 1);
                            entry.name[sizeof(entry.name) - 1] = '\0';
                            entry.priority = 200 + g_config.game_queue_count;
                            entry.status = GAME_AUTO_DISCOVERED;
                            entry.progress_pct = pct;
                            entry.minutes_watched = maxCurWatch;
                            g_config.game_queue_count++;
                            Logger::info("Auto-queued account campaign: '%s' (Progress: %d%%)", gName, pct);
                            StorageManager::saveConfig(g_config);
                        }
                    }
                }
            } else {
                Logger::warn("No Twitch Drop campaigns found for '%s'. Check twitch.tv/drops/campaigns!", g_state.user_login);
            }
        }
    }
    return true;
}

bool TwitchAPI::findLiveStreamForGame(const char* gameName) {
    if (g_config.target_channel[0] != '\0') {
        snprintf(g_state.current_channel, sizeof(g_state.current_channel), "%s", g_config.target_channel);
        Logger::info("Using specified target streamer: %s", g_state.current_channel);
    } else {
        const char* searchGame = (g_config.target_game[0] != '\0') ? g_config.target_game : ((g_state.current_game[0] != '\0') ? g_state.current_game : gameName);
        Logger::info("Searching top Drops-enabled stream for game: %s ...", searchGame);
        
        String payload = "[{\"operationName\":\"DirectoryPage_Game\",\"query\":\"query DirectoryPage_Game($name: String!) { game(name: $name) { id streams(first: 15) { edges { node { id freeformTags { name } broadcaster { id login } } } } } }\",\"variables\":{\"name\":\"";
        payload += searchGame;
        payload += "\"}}]";

        String response = sendGraphQLRequest(payload);
        if (response.length() > 0) {
            JsonDocument doc;
            if (!deserializeJson(doc, response)) {
                JsonArray edges = doc[0]["data"]["game"]["streams"]["edges"].as<JsonArray>();
                if (edges.size() > 0) {
                    JsonObject selectedNode;
                    bool streamerFound = false;

                    // 1. First priority: look for a live stream with "drop" tag that is NOT in the failed blacklist
                    for (JsonObject edge : edges) {
                        JsonObject node = edge["node"].as<JsonObject>();
                        const char* login = node["broadcaster"]["login"] | "";
                        if (strlen(login) == 0 || isStreamerFailed(login)) continue;

                        JsonArray tags = node["freeformTags"].as<JsonArray>();
                        for (JsonObject tag : tags) {
                            const char* tagName = tag["name"] | "";
                            if (strcasestr(tagName, "drop") != NULL) {
                                selectedNode = node;
                                streamerFound = true;
                                Logger::info("Selected streamer '%s' with Drops Enabled tag ('%s')", login, tagName);
                                break;
                            }
                        }
                        if (streamerFound) break;
                    }

                    // 2. Second priority: pick first stream from category that is NOT blacklisted
                    if (!streamerFound) {
                        for (JsonObject edge : edges) {
                            JsonObject node = edge["node"].as<JsonObject>();
                            const char* login = node["broadcaster"]["login"] | "";
                            if (strlen(login) > 0 && !isStreamerFailed(login)) {
                                selectedNode = node;
                                streamerFound = true;
                                Logger::info("Selected stream for '%s': '%s'", searchGame, login);
                                break;
                            }
                        }
                    }

                    if (streamerFound) {
                        JsonObject broadcaster = selectedNode["broadcaster"];
                        const char* login = broadcaster["login"] | "";
                        const char* channelId = broadcaster["id"] | "";
                        const char* broadcastId = selectedNode["id"] | "";

                        snprintf(g_state.current_channel, sizeof(g_state.current_channel), "%s", login);
                        snprintf(g_state.current_channel_id, sizeof(g_state.current_channel_id), "%s", channelId);
                        snprintf(g_state.current_broadcast_id, sizeof(g_state.current_broadcast_id), "%s", broadcastId);

                        Logger::info("Found live streamer: %s (Channel ID: %s, Broadcast ID: %s)", login, channelId, broadcastId);
                        return true;
                    }
                }
            }
        }
        Logger::warn("No eligible live streams with Drops found for '%s'. Rotating to next game...", searchGame);
        // Demote stalled game and switch to next game in queue
        for (uint8_t qi = 0; qi < g_config.game_queue_count; qi++) {
            if (strcasecmp(g_config.game_queue[qi].name, searchGame) == 0) {
                g_config.game_queue[qi].status = GAME_QUEUED;
                g_config.game_queue[qi].priority += 10;
                break;
            }
        }
        selectNextGameFromQueue();
        return false;
    }

    // Lookup channel details & live broadcast ID for target channel if missing
    if (strlen(g_state.current_channel) > 0 && (strlen(g_state.current_channel_id) == 0 || strlen(g_state.current_broadcast_id) == 0)) {
        String payload = "[{\"operationName\":\"VideoPlayerStreamInfoOverlayChannel\",\"query\":\"query VideoPlayerStreamInfoOverlayChannel($channel: String!) { user(login: $channel) { id stream { id createdAt type viewersCount } } }\",\"variables\":{\"channel\":\"";
        payload += g_state.current_channel;
        payload += "\"}}]";

        String response = sendGraphQLRequest(payload);
        if (response.length() > 0) {
            JsonDocument doc;
            if (!deserializeJson(doc, response)) {
                JsonObject userObj = doc[0]["data"]["user"];
                const char* idStr = userObj["id"] | "";
                const char* bIdStr = userObj["stream"]["id"] | "";

                if (strlen(idStr) > 0) snprintf(g_state.current_channel_id, sizeof(g_state.current_channel_id), "%s", idStr);
                if (strlen(bIdStr) > 0) snprintf(g_state.current_broadcast_id, sizeof(g_state.current_broadcast_id), "%s", bIdStr);

                Logger::info("Streamer metadata updated: %s (Channel ID: %s, Broadcast ID: %s)", g_state.current_channel, g_state.current_channel_id, g_state.current_broadcast_id);
                return true;
            }
        }
    }
    return strlen(g_state.current_channel_id) > 0;
}

String TwitchAPI::urlEncode(const String& str) {
    String encoded = "";
    char c;
    char code0;
    char code1;
    for (unsigned int i = 0; i < str.length(); i++) {
        c = str.charAt(i);
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            code0 = (c >> 4) & 0xF;
            code1 = c & 0xF;
            encoded += '%';
            encoded += (char)(code0 < 10 ? code0 + '0' : code0 - 10 + 'A');
            encoded += (char)(code1 < 10 ? code1 + '0' : code1 - 10 + 'A');
        }
    }
    return encoded;
}

bool TwitchAPI::fetchPlaybackAccessToken(const char* channelLogin, String& outSig, String& outValue, String& outSessionId) {
    if (strlen(channelLogin) == 0) return false;

    String payload = "[{\"operationName\":\"PlaybackAccessToken\",\"query\":\"query PlaybackAccessToken($login: String!, $playerType: String!) { streamPlaybackAccessToken(channelName: $login, params: {platform: \\\"web\\\", playerBackend: \\\"mediaplayer\\\", playerType: $playerType}) { value signature } }\",\"variables\":{\"login\":\"";
    payload += channelLogin;
    payload += "\",\"playerType\":\"site\"}}]";

    String response = sendGraphQLRequest(payload);
    if (response.length() == 0) return false;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) return false;

    JsonArray root = doc.as<JsonArray>();
    if (root.size() > 0 && root[0]["data"]["streamPlaybackAccessToken"].is<JsonObject>()) {
        JsonObject tokenObj = root[0]["data"]["streamPlaybackAccessToken"];
        outValue = tokenObj["value"] | "";
        outSig = tokenObj["signature"] | "";

        if (outValue.length() > 0) {
            // 1. Try parsing JSON string inside outValue
            JsonDocument innerDoc;
            if (!deserializeJson(innerDoc, outValue)) {
                const char* sid = innerDoc["session_id"] | "";
                if (strlen(sid) > 0) {
                    outSessionId = sid;
                }
            }

            // 2. Fallback: String search if inner deserialize omitted session_id
            if (outSessionId.length() == 0) {
                int idx = outValue.indexOf("\"session_id\":\"");
                if (idx != -1) {
                    int start = idx + 14;
                    int end = outValue.indexOf("\"", start);
                    if (end != -1) {
                        outSessionId = outValue.substring(start, end);
                    }
                }
            }

            // 3. Fallback: Hardware TRNG 32-hex session ID generator matching Twitch Web Player
            if (outSessionId.length() == 0) {
                char genSid[40];
                snprintf(genSid, sizeof(genSid), "%08x%08x%08x%08x", 
                    esp_random(), esp_random(), esp_random(), esp_random());
                outSessionId = genSid;
            }

            strncpy(g_session_id, outSessionId.c_str(), sizeof(g_session_id));
            Logger::info("Acquired Stream PlaybackAccessToken for '%s' (Session ID: %s)", channelLogin, outSessionId.c_str());
            return true;
        }
    }
    return false;
}

bool TwitchAPI::pingUsherPlaylist(const char* channelLogin, const String& sig, const String& value) {
    if (strlen(channelLogin) == 0 || sig.length() == 0 || value.length() == 0) return false;

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String url = "https://usher.ttvnw.net/api/channel/hls/";
    url += channelLogin;
    url += ".m3u8?sig=";
    url += sig;
    url += "&token=";
    url += urlEncode(value);
    url += "&allow_source=true";

    http.begin(client, url);
    http.setTimeout(8000);

    http.addHeader("Client-ID", getClientId());
    http.addHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36");

    int httpCode = http.GET();
    bool ok = (httpCode == HTTP_CODE_OK);
    if (ok) {
        Logger::info("Usher HLS Video Session pinged (HTTP 200 OK)");
    } else {
        Logger::warn("Usher HLS Video Session status: HTTP %d", httpCode);
    }
    http.end();
    return ok;
}

static const char b64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64Encode(const uint8_t* data, size_t length) {
    String encoded = "";
    encoded.reserve(((length + 2) / 3) * 4);
    for (size_t i = 0; i < length; i += 3) {
        uint32_t b = (data[i] << 16);
        if (i + 1 < length) b |= (data[i + 1] << 8);
        if (i + 2 < length) b |= data[i + 2];

        encoded += b64_alphabet[(b >> 18) & 0x3F];
        encoded += b64_alphabet[(b >> 12) & 0x3F];
        encoded += (i + 1 < length) ? b64_alphabet[(b >> 6) & 0x3F] : '=';
        encoded += (i + 2 < length) ? b64_alphabet[b & 0x3F] : '=';
    }
    return encoded;
}



// Generate ISO 8601 timestamp for client_time field
static String getIsoTimestamp() {
    time_t now = time(NULL);
    if (now > 1700000000) { // Valid synchronized NTP time
        struct tm* tm_info = gmtime(&now);
        char buf[30];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm_info);
        return String(buf);
    }
    // Dynamic fallback: use baseline Unix timestamp + millis offset (never static duplicate)
    static time_t baseEpoch = 1770000000;
    time_t dynamicEpoch = baseEpoch + (millis() / 1000);
    struct tm* tm_info = gmtime(&dynamicEpoch);
    char buf[30];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm_info);
    return String(buf);
}

// Build the _watch_payload JSON string matching Twitch web client & TwitchDropsMiner
static String buildWatchPayload(const char* channelId, const char* broadcastId) {
    String payload = "[{\"event\":\"minute-watched\",\"properties\":{";
    payload += "\"broadcast_id\":\""; payload += (broadcastId && broadcastId[0] ? broadcastId : "0"); payload += "\",";
    payload += "\"channel_id\":\""; payload += channelId; payload += "\",";
    payload += "\"channel\":\""; payload += g_state.current_channel; payload += "\",";
    payload += "\"client_time\":\""; payload += getIsoTimestamp(); payload += "\",";
    payload += "\"device_id\":\""; payload += g_state.device_id; payload += "\",";
    payload += "\"game\":\""; payload += (g_state.current_game[0] ? g_state.current_game : ""); payload += "\",";
    payload += "\"game_id\":\""; payload += (g_state.current_game_id[0] ? g_state.current_game_id : ""); payload += "\",";
    payload += "\"hidden\":false,";
    payload += "\"is_live\":true,";
    payload += "\"live\":true,";
    payload += "\"location\":\"channel\",";
    payload += "\"logged_in\":true,";
    payload += "\"minutes_logged\":1,";
    payload += "\"muted\":false,";
    payload += "\"player\":\"site\",";
    payload += "\"user_id\":\""; payload += g_state.user_id; payload += "\"";
    payload += "}}]";
    return payload;
}

// Helper: Raw deflate memory compression with PSRAM/HEAP-allocated tdefl_compressor struct
// Avoids ESP-ROM tdefl_compress_mem_to_mem stack overflow (~128KB stack allocation)
static size_t heap_tdefl_compress_mem_to_mem(void *pOut_buf, size_t out_buf_len, const void *pIn_buf, size_t in_buf_len, int flags) {
    tdefl_compressor *pComp = NULL;
#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
    if (psramFound()) {
        pComp = (tdefl_compressor *)ps_malloc(sizeof(tdefl_compressor));
    }
#endif
    if (!pComp) {
        pComp = (tdefl_compressor *)malloc(sizeof(tdefl_compressor));
    }
    if (!pComp) {
        return 0;
    }

    if (tdefl_init(pComp, NULL, NULL, flags) != TDEFL_STATUS_OKAY) {
        free(pComp);
        return 0;
    }

    size_t out_buf_ofs = 0;
    const uint8_t *pIn = (const uint8_t *)pIn_buf;

    for (;;) {
        size_t in_buf_size = in_buf_len;
        size_t out_buf_size = out_buf_len - out_buf_ofs;

        tdefl_status status = tdefl_compress(pComp, pIn, &in_buf_size, (uint8_t *)pOut_buf + out_buf_ofs, &out_buf_size, TDEFL_FINISH);

        pIn += in_buf_size;
        in_buf_len -= in_buf_size;
        out_buf_ofs += out_buf_size;

        if (status == TDEFL_STATUS_DONE) {
            break;
        } else if (status != TDEFL_STATUS_OKAY) {
            out_buf_ofs = 0;
            break;
        }
    }

    free(pComp);
    return out_buf_ofs;
}

// GQL mutation sendSpadeEvents with gzip + base64 encoding
// This matches TwitchDropsMiner channel.py gql_payload property
// Uses ESP32 ROM miniz for raw deflate, then wraps in gzip header/footer manually.
// Fallback: Uses stored (uncompressed) GZIP deflate block if dynamic memory allocation fails.
bool TwitchAPI::sendGqlSpadeEvents(const char* channelId, const char* broadcastId) {
    String watchPayload = buildWatchPayload(channelId, broadcastId);
    
    size_t inputLen = watchPayload.length();
    const uint8_t* inputData = (const uint8_t*)watchPayload.c_str();
    
    uint8_t* gzipBuf = NULL;
    size_t gzipLen = 0;

    // Try standard deflate compression
    size_t maxDeflateLen = inputLen + 256;
    uint8_t* deflateBuf = (uint8_t*)malloc(maxDeflateLen);
    size_t deflatedLen = 0;

    if (deflateBuf) {
        deflatedLen = heap_tdefl_compress_mem_to_mem(deflateBuf, maxDeflateLen, inputData, inputLen, TDEFL_DEFAULT_MAX_PROBES);
    }

    if (deflatedLen > 0 && deflateBuf) {
        // Deflate succeeded: [10-byte gzip header] + [deflated data] + [4-byte CRC32] + [4-byte ISIZE]
        gzipLen = 10 + deflatedLen + 8;
        gzipBuf = (uint8_t*)malloc(gzipLen);
        if (gzipBuf) {
            gzipBuf[0] = 0x1F;
            gzipBuf[1] = 0x8B;
            gzipBuf[2] = 0x08; // CM = deflate
            gzipBuf[3] = 0x00; // FLG
            gzipBuf[4] = gzipBuf[5] = gzipBuf[6] = gzipBuf[7] = 0x00;
            gzipBuf[8] = 0x00;
            gzipBuf[9] = 0xFF;
            
            memcpy(gzipBuf + 10, deflateBuf, deflatedLen);
            
            uint32_t crc = esp_rom_crc32_le(0, inputData, inputLen);
            gzipBuf[10 + deflatedLen + 0] = (crc >>  0) & 0xFF;
            gzipBuf[10 + deflatedLen + 1] = (crc >>  8) & 0xFF;
            gzipBuf[10 + deflatedLen + 2] = (crc >> 16) & 0xFF;
            gzipBuf[10 + deflatedLen + 3] = (crc >> 24) & 0xFF;
            
            uint32_t isize = (uint32_t)inputLen;
            gzipBuf[10 + deflatedLen + 4] = (isize >>  0) & 0xFF;
            gzipBuf[10 + deflatedLen + 5] = (isize >>  8) & 0xFF;
            gzipBuf[10 + deflatedLen + 6] = (isize >> 16) & 0xFF;
            gzipBuf[10 + deflatedLen + 7] = (isize >> 24) & 0xFF;
        }
        free(deflateBuf);
    } else {
        if (deflateBuf) free(deflateBuf);
        Logger::warn("Deflate RAM allocation/compression skipped. Using zero-RAM GZIP stored block fallback.");
    }

    // Fallback: RFC-1951/1952 stored (uncompressed) GZIP block (requires 0 compressor RAM)
    if (!gzipBuf) {
        // [10-byte header] + [5-byte stored block header] + [inputLen payload] + [8-byte trailer]
        gzipLen = 10 + 5 + inputLen + 8;
        gzipBuf = (uint8_t*)malloc(gzipLen);
        if (!gzipBuf) {
            Logger::error("Failed to allocate gzip buffer for fallback");
            return false;
        }

        gzipBuf[0] = 0x1F;
        gzipBuf[1] = 0x8B;
        gzipBuf[2] = 0x08; // CM = deflate
        gzipBuf[3] = 0x00; // FLG
        gzipBuf[4] = gzipBuf[5] = gzipBuf[6] = gzipBuf[7] = 0x00;
        gzipBuf[8] = 0x00;
        gzipBuf[9] = 0xFF;

        // Deflate stored block header: BFINAL=1, BTYPE=00 (uncompressed block)
        gzipBuf[10] = 0x01;
        uint16_t len16 = (uint16_t)inputLen;
        uint16_t nlen16 = ~len16;
        gzipBuf[11] = len16 & 0xFF;
        gzipBuf[12] = (len16 >> 8) & 0xFF;
        gzipBuf[13] = nlen16 & 0xFF;
        gzipBuf[14] = (nlen16 >> 8) & 0xFF;

        memcpy(gzipBuf + 15, inputData, inputLen);

        uint32_t crc = esp_rom_crc32_le(0, inputData, inputLen);
        size_t trailerIdx = 15 + inputLen;
        gzipBuf[trailerIdx + 0] = (crc >>  0) & 0xFF;
        gzipBuf[trailerIdx + 1] = (crc >>  8) & 0xFF;
        gzipBuf[trailerIdx + 2] = (crc >> 16) & 0xFF;
        gzipBuf[trailerIdx + 3] = (crc >> 24) & 0xFF;

        uint32_t isize = (uint32_t)inputLen;
        gzipBuf[trailerIdx + 4] = (isize >>  0) & 0xFF;
        gzipBuf[trailerIdx + 5] = (isize >>  8) & 0xFF;
        gzipBuf[trailerIdx + 6] = (isize >> 16) & 0xFF;
        gzipBuf[trailerIdx + 7] = (isize >> 24) & 0xFF;
    }

    // Base64 encode the gzip compressed data
    String b64Data = base64Encode(gzipBuf, gzipLen);
    free(gzipBuf);

    // Build GQL mutation payload
    String gqlPayload = "[{\"operationName\":\"SendEvents\",\"query\":\"mutation SendEvents($input: SendSpadeEventsInput!) { sendSpadeEvents(input: $input) { statusCode } }\",\"variables\":{\"input\":{\"data\":\"";
    gqlPayload += b64Data;
    gqlPayload += "\"}}}]";

    String response = sendGraphQLRequest(gqlPayload);
    if (response.length() > 0 && response.indexOf("\"errors\"") == -1) {
        Logger::info("GQL SendSpadeEvents accepted (mutation OK)");
        return true;
    } else {
        Logger::warn("GQL SendSpadeEvents failed, response: %s", response.length() > 200 ? response.substring(0, 200).c_str() : response.c_str());
        return false;
    }
}

bool TwitchAPI::sendSpadeBeacon(const char* channelId, const char* broadcastId, const char* sessionId) {
    String token = getCleanToken();
    if (token.length() == 0 || strlen(channelId) == 0) return false;

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, "https://spade.twitch.tv/track");
    http.setTimeout(8000);

    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("Client-ID", getClientId());
    
    String authHeader = "OAuth ";
    authHeader += token;
    http.addHeader("Authorization", authHeader);
    http.addHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36");
    http.addHeader("X-Device-Id", g_state.device_id);
    if (g_session_id[0] != '\0') {
        http.addHeader("Client-Session-Id", g_session_id);
    }
    if (strlen(g_integrity_token) > 0) {
        http.addHeader("Client-Integrity", g_integrity_token);
    }

    String watchPayload = buildWatchPayload(channelId, broadcastId);
    String b64Data = base64Encode((const uint8_t*)watchPayload.c_str(), watchPayload.length());
    String postBody = "data=" + urlEncode(b64Data);

    int httpCode = http.POST(postBody);
    bool ok = (httpCode >= 200 && httpCode < 300);
    if (ok) {
        Logger::info("Spade Base64 Telemetry Beacon accepted (HTTP %d)", httpCode);
    } else {
        Logger::warn("Spade Base64 Telemetry Beacon status: HTTP %d", httpCode);
    }
    http.end();
    return ok;
}

bool TwitchAPI::sendMinuteWatchedHeartbeat(const char* channelLogin, const char* channelId, const char* broadcastId) {
    if (strlen(channelLogin) == 0 || strlen(channelId) == 0) {
        Logger::warn("Cannot send heartbeat: No active streamer channel/ID selected.");
        return false;
    }

    static uint32_t lastAccessTokenFetch = 0;
    static String cachedSig = "";
    static String cachedValue = "";
    static String cachedSessionId = "";

    uint32_t now = millis();
    if (cachedSessionId.length() == 0 || lastAccessTokenFetch == 0 || (now - lastAccessTokenFetch) >= 900000) {
        if (fetchPlaybackAccessToken(channelLogin, cachedSig, cachedValue, cachedSessionId)) {
            lastAccessTokenFetch = millis();
        }
    }

    // Ping Usher HLS playlist every minute to keep stream playback session active on Twitch
    bool usherOk = false;
    static uint8_t offlineRetryCount = 0;
    if (cachedSig.length() > 0 && cachedValue.length() > 0) {
        usherOk = pingUsherPlaylist(channelLogin, cachedSig, cachedValue);
    }

    // If streamer is offline (Usher 404/403) and no pinned channel, auto-clear to trigger re-search
    if (!usherOk && g_config.target_channel[0] == '\0') {
        offlineRetryCount++;
        if (offlineRetryCount >= 2) {
            Logger::warn("Streamer '%s' appears offline or restricted (Usher error x%d). Auto-rotating...", channelLogin, offlineRetryCount);
            markStreamerFailed(channelLogin);
            g_state.current_channel[0] = '\0';
            g_state.current_channel_id[0] = '\0';
            g_state.current_broadcast_id[0] = '\0';
            cachedSig = "";
            cachedValue = "";
            cachedSessionId = "";
            lastAccessTokenFetch = 0;
            offlineRetryCount = 0;
            return false;
        }
    } else {
        offlineRetryCount = 0;
    }

    // Skip heartbeat if broadcast ID is empty (streamer definitely offline)
    if (strlen(broadcastId) == 0 && !usherOk) {
        Logger::warn("Skipping heartbeat: no broadcast ID and Usher offline for '%s'", channelLogin);
        return false;
    }

    Logger::info("Sending MinuteWatched Heartbeat for '%s' (Channel ID: %s, Time: %s)...", channelLogin, channelId, getIsoTimestamp().c_str());

    // 1. Primary: GQL mutation sendSpadeEvents (gzip + base64)
    bool sentGql = sendGqlSpadeEvents(channelId, broadcastId);
    
    // 2. Secondary Telemetry Beacon: Spade URL POST (base64)
    bool sentSpade = sendSpadeBeacon(channelId, broadcastId, cachedSessionId.c_str());

    g_state.total_minutes_watched++;
    g_state.last_heartbeat_time = millis();

    // 3. Live refresh inventory progress every 2 minutes
    static uint32_t lastInventoryFetch = 0;
    if (lastInventoryFetch == 0 || (millis() - lastInventoryFetch) >= 120000) {
        fetchInventoryAndProgress();
        lastInventoryFetch = millis();
    }

    Logger::info("Heartbeat complete! Minutes Farmed: %u min (Current Progress: %d%%)", g_state.total_minutes_watched, g_state.drop_progress_pct);
    snprintf(g_state.status_message, sizeof(g_state.status_message), "Farming %s (%d%%)", channelLogin, g_state.drop_progress_pct);
    return (sentGql || sentSpade);
}

bool TwitchAPI::claimDrop(const char* dropInstanceId) {
    if (strlen(dropInstanceId) == 0) {
        Logger::warn("Cannot claim drop: Empty dropInstanceID.");
        return false;
    }

    Logger::info("Claiming drop reward (Instance ID: %s)...", dropInstanceId);

    // Twitch Persisted Query for DropsPage_ClaimDropRewards
    // Sha256Hash: 2f884fa187b8fadb2a49db0adc033e636f7b6aaee6e76de1e2bba9a7baf0daf6
    String payload = "[{\"operationName\":\"DropsPage_ClaimDropRewards\",\"extensions\":{\"persistedQuery\":{\"version\":1,\"sha256Hash\":\"2f884fa187b8fadb2a49db0adc033e636f7b6aaee6e76de1e2bba9a7baf0daf6\"}},\"variables\":{\"input\":{\"dropInstanceID\":\"";
    payload += dropInstanceId;
    payload += "\"}}}]";

    String response = sendGraphQLRequest(payload);
    if (response.length() == 0) {
        Logger::warn("Claim drop request failed: empty response");
        return false;
    }

    JsonDocument doc;
    if (!deserializeJson(doc, response)) {
        JsonObject claimObj = doc[0]["data"]["claimDropRewards"];
        if (claimObj.isNull()) {
            claimObj = doc[0]["data"]["claimDrop"];
        }
        if (claimObj.isNull()) {
            Logger::warn("Claim drop response returned null/error: %s", response.c_str());
            return false;
        }
        const char* status = claimObj["status"] | "";
        if (strcmp(status, "SUCCESS") == 0 || strcmp(status, "FULFILLED") == 0 || 
            strcmp(status, "ELIGIBLE_FOR_ALL") == 0 || strcmp(status, "DROP_INSTANCE_ID") == 0) {
            g_state.drops_claimed_count++;
            Logger::info("🎉 DROP CLAIMED SUCCESSFULLY! Total claimed: %u", g_state.drops_claimed_count);
            snprintf(g_state.status_message, sizeof(g_state.status_message), "Claimed drop! Total: %u", g_state.drops_claimed_count);
            return true;
        } else if (strcmp(status, "DROP_INSTANCE_ALREADY_CLAIMED") == 0 || strcmp(status, "ALREADY_CLAIMED") == 0) {
            Logger::info("Drop already claimed on Twitch (status: %s).", status);
            return true;
        } else {
            Logger::warn("Claim response status: '%s'", status[0] ? status : "unknown");
            return false;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Twitch PubSub WebSockets Engine (wss://pubsub-edge.twitch.tv/v1)
// ---------------------------------------------------------------------------

static bool pubsubStarted = false;
static uint32_t pubsubConnectStartTime = 0;
static uint32_t pubsubNextAttemptTime = 0;
static uint8_t pubsubFailCount = 0;

static void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            g_state.pubsub_connected = false;
            pubsubStarted = false;
            Logger::warn("Twitch PubSub WebSocket disconnected.");
            break;
        case WStype_CONNECTED:
            g_state.pubsub_connected = true;
            pubsubFailCount = 0;
            Logger::info("Connected to Twitch PubSub WebSocket (wss://pubsub-edge.twitch.tv/v1)!");
            TwitchAPI::subscribePubSubTopics();
            break;
        case WStype_TEXT: {
            String msg = (char*)payload;
            if (msg.indexOf("\"PONG\"") != -1) {
                // Ping reply received
            } else if (msg.indexOf("\"drop-progress\"") != -1 || msg.indexOf("\"drop-claim\"") != -1) {
                Logger::info("PubSub Drop Event received: %s", msg.c_str());
                TwitchAPI::fetchInventoryAndProgress();
            }
            break;
        }
        default:
            break;
    }
}

void TwitchAPI::connectPubSub() {
    if (pubsubStarted || g_state.pubsub_connected) return;

    uint32_t now = millis();
    if (pubsubNextAttemptTime > 0 && now < pubsubNextAttemptTime) return;

    String token = getCleanToken();
    if (token.length() == 0) return;

    pubsubStarted = true;
    pubsubConnectStartTime = now;
    Logger::info("Connecting to Twitch PubSub WebSocket...");
    wsClient.beginSSL("pubsub-edge.twitch.tv", 443, "/v1");
    wsClient.onEvent(webSocketEvent);
    wsClient.setReconnectInterval(15000);
}

void TwitchAPI::disconnectPubSub() {
    wsClient.disconnect();
    g_state.pubsub_connected = false;
    pubsubStarted = false;
}

void TwitchAPI::subscribePubSubTopics() {
    if (!g_state.pubsub_connected) return;

    String token = getCleanToken();
    if (token.length() == 0) return;

    JsonDocument doc;
    doc["type"] = "LISTEN";

    JsonObject data = doc["data"].to<JsonObject>();
    data["auth_token"] = token;

    JsonArray topics = data["topics"].to<JsonArray>();
    
    // 1. Subscribe to streamer playback events
    if (strlen(g_state.current_channel_id) > 0) {
        String playbackTopic = "video-playback-by-id.";
        playbackTopic += g_state.current_channel_id;
        topics.add(playbackTopic);
        strncpy(currentSubscribedChannelId, g_state.current_channel_id, sizeof(currentSubscribedChannelId));
    }

    // 2. Subscribe to user drop events
    if (strlen(g_state.user_id) > 0) {
        String dropTopic = "user-drop-events.";
        dropTopic += g_state.user_id;
        topics.add(dropTopic);
    }

    String jsonStr;
    serializeJson(doc, jsonStr);
    wsClient.sendTXT(jsonStr);
    Logger::info("Subscribed to Twitch PubSub topics for channel '%s' (ID: %s)", g_state.current_channel, g_state.current_channel_id);
}

void TwitchAPI::sendPubSubPing() {
    if (!g_state.pubsub_connected) return;
    wsClient.sendTXT("{\"type\":\"PING\"}");
}

void TwitchAPI::loopPubSub() {
    if (!g_state.wifi_connected || getCleanToken().length() == 0) {
        if (pubsubStarted) {
            disconnectPubSub();
        }
        return;
    }

    uint32_t now = millis();

    // 1. Check attempt cooldown if PubSub is not started
    if (!pubsubStarted && !g_state.pubsub_connected) {
        if (pubsubNextAttemptTime == 0 || now >= pubsubNextAttemptTime) {
            connectPubSub();
        }
        return;
    }

    // 2. If connecting for more than 15 seconds without success, abort attempt & set 5-minute backoff
    if (pubsubStarted && !g_state.pubsub_connected) {
        if (now - pubsubConnectStartTime >= 15000) {
            pubsubFailCount++;
            pubsubNextAttemptTime = now + 300000; // Wait 5 minutes before trying SSL WebSocket again
            Logger::warn("PubSub SSL connection timed out. Falling back to HTTP GraphQL farming (next PubSub retry in 5m).");
            disconnectPubSub();
            return;
        }
    }

    // 3. Process WebSocket loop if started
    wsClient.loop();

    if (g_state.pubsub_connected) {
        // Send PING every 3 minutes (180,000 ms)
        if (now - lastPubSubPingTime >= 180000) {
            sendPubSubPing();
            lastPubSubPingTime = now;
        }

        // Re-subscribe if current channel ID changed
        if (strlen(g_state.current_channel_id) > 0 && strcmp(currentSubscribedChannelId, g_state.current_channel_id) != 0) {
            subscribePubSubTopics();
        }
    }
}
