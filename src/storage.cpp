#include "storage.h"
#include "logger.h"
#include <Preferences.h>
#include <ArduinoJson.h>

static Preferences prefs;
const char* NVS_NAMESPACE = "twitch_farm";

bool StorageManager::init() {
    Logger::info("Initializing NVS Storage...");
    return true;
}

// Deserialize game queue from NVS JSON string
static void loadGameQueue(AppConfig& config, const String& jsonStr) {
    config.game_queue_count = 0;
    if (jsonStr.length() == 0) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonStr);
    if (err) {
        Logger::warn("Failed to parse games_q JSON: %s", err.c_str());
        return;
    }

    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
        if (config.game_queue_count >= MAX_PRIORITY_GAMES) break;
        GameEntry& entry = config.game_queue[config.game_queue_count];
        strncpy(entry.name, obj["n"] | "", sizeof(entry.name) - 1);
        entry.name[sizeof(entry.name) - 1] = '\0';
        entry.priority = obj["p"] | (config.game_queue_count + 1);
        entry.status = (GameStatus)(obj["s"].as<uint8_t>());
        entry.progress_pct = obj["pct"] | 0;
        entry.minutes_watched = obj["mw"] | 0;
        if (strlen(entry.name) > 0) {
            config.game_queue_count++;
        }
    }
}

// Serialize game queue to compact JSON string
static String serializeGameQueue(const AppConfig& config) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (uint8_t i = 0; i < config.game_queue_count; i++) {
        const GameEntry& e = config.game_queue[i];
        if (strlen(e.name) == 0) continue;
        JsonObject obj = arr.add<JsonObject>();
        obj["n"] = e.name;
        obj["p"] = e.priority;
        obj["s"] = (uint8_t)e.status;
        obj["pct"] = e.progress_pct;
        obj["mw"] = e.minutes_watched;
    }

    String output;
    serializeJson(doc, output);
    return output;
}

bool StorageManager::loadConfig(AppConfig& config) {
    if (!prefs.begin(NVS_NAMESPACE, true)) { // Read-only mode
        Logger::warn("NVS namespace not found or empty. Using default settings.");
        // Set defaults
        strncpy(config.wifi_ssid, "", sizeof(config.wifi_ssid));
        strncpy(config.wifi_pass, "", sizeof(config.wifi_pass));
        strncpy(config.oauth_token, "", sizeof(config.oauth_token));
        strncpy(config.client_id, DEFAULT_CLIENT_ID, sizeof(config.client_id));
        strncpy(config.target_game, "", sizeof(config.target_game));
        strncpy(config.target_channel, "", sizeof(config.target_channel));
        config.auto_claim = true;
        config.check_interval_sec = 60;
        config.farming_enabled = true;
        config.game_queue_count = 0;
        return false;
    }

    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    String token = prefs.getString("token", "");
    String clientId = prefs.getString("client_id", DEFAULT_CLIENT_ID);
    String game = prefs.getString("game", "");
    String channel = prefs.getString("channel", "");
    bool autoClaim = prefs.getBool("auto_claim", true);
    uint16_t interval = prefs.getUShort("interval", 60);
    bool enabled = prefs.getBool("enabled", true);
    String gamesJson = prefs.getString("games_q", "");

    prefs.end();

    strncpy(config.wifi_ssid, ssid.c_str(), sizeof(config.wifi_ssid));
    strncpy(config.wifi_pass, pass.c_str(), sizeof(config.wifi_pass));
    strncpy(config.oauth_token, token.c_str(), sizeof(config.oauth_token));
    strncpy(config.client_id, clientId.c_str(), sizeof(config.client_id));
    strncpy(config.target_game, game.c_str(), sizeof(config.target_game));
    strncpy(config.target_channel, channel.c_str(), sizeof(config.target_channel));
    config.auto_claim = autoClaim;
    config.check_interval_sec = interval;
    config.farming_enabled = enabled;

    // Load priority game queue
    loadGameQueue(config, gamesJson);

    // Backward migration: if no queue exists but old single game is set, create 1-entry queue
    if (config.game_queue_count == 0 && strlen(config.target_game) > 0) {
        GameEntry& entry = config.game_queue[0];
        strncpy(entry.name, config.target_game, sizeof(entry.name) - 1);
        entry.name[sizeof(entry.name) - 1] = '\0';
        entry.priority = 1;
        entry.status = GAME_QUEUED;
        entry.progress_pct = 0;
        entry.minutes_watched = 0;
        config.game_queue_count = 1;
        Logger::info("Migrated single game '%s' to priority queue", config.target_game);
    }

    Logger::info("Config loaded from NVS. Target Game: %s, Queue: %d games, Wi-Fi: %s",
        config.target_game[0] ? config.target_game : "<auto>",
        config.game_queue_count,
        config.wifi_ssid[0] ? config.wifi_ssid : "<not set>");
    return true;
}

bool StorageManager::saveConfig(const AppConfig& config) {
    if (!prefs.begin(NVS_NAMESPACE, false)) { // Read-Write mode
        Logger::error("Failed to open NVS namespace for writing.");
        return false;
    }

    prefs.putString("ssid", config.wifi_ssid);
    prefs.putString("pass", config.wifi_pass);
    prefs.putString("token", config.oauth_token);
    prefs.putString("client_id", config.client_id);
    prefs.putString("game", config.target_game);
    prefs.putString("channel", config.target_channel);
    prefs.putBool("auto_claim", config.auto_claim);
    prefs.putUShort("interval", config.check_interval_sec);
    prefs.putBool("enabled", config.farming_enabled);

    // Serialize and save game queue
    String gamesJson = serializeGameQueue(config);
    prefs.putString("games_q", gamesJson);

    prefs.end();
    Logger::info("Config successfully saved to NVS.");
    return true;
}

void StorageManager::resetConfig() {
    if (prefs.begin(NVS_NAMESPACE, false)) {
        prefs.clear();
        prefs.end();
        Logger::info("NVS storage cleared.");
    }
}

