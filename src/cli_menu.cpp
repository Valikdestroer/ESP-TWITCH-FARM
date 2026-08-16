#include "cli_menu.h"
#include "logger.h"
#include "storage.h"
#include "wifi_mgr.h"
#include "twitch_api.h"
#include "led_indicator.h"

void CLIMenu::init() {
    printBanner();
}

void CLIMenu::printBanner() {
    Serial.println("\n=======================================================");
    Serial.printf(  "   ESP32-S3 Twitch Drops Farmer v%s\n", FIRMWARE_VERSION);
    Serial.println(  "   Target MCU: ESP32-S3-WROOM-1 (N16R8)");
    Serial.println(  "=======================================================");
    Serial.println(  " Available USB Commands:");
    Serial.println(  "  1  | status                        - Display current status & stats");
    Serial.println(  "  2  | wifi <ssid> <pass>            - Configure Wi-Fi network");
    Serial.println(  "  3  | token <oauth_token>           - Set active Twitch OAuth token");
    Serial.println(  "  4  | game add <name> [prio]        - Add game to priority queue");
    Serial.println(  "     | game priority <name> <val>    - Set game priority (e.g. game prio Rust 1)");
    Serial.println(  "     | game up/down <name> [steps]   - Reorder games in queue");
    Serial.println(  "     | game remove <name>            - Remove game from queue");
    Serial.println(  "     | game streamer add <g> <s>     - Add preferred streamer for game");
    Serial.println(  "     | game streamer remove <g> <s>  - Remove preferred streamer for game");
    Serial.println(  "     | game streamer clear <g>       - Clear streamers for game");
    Serial.println(  "     | game list                     - Show priority queue with streamers");
    Serial.println(  "     | game clear                    - Clear all games from queue");
    Serial.println(  "  5  | channel <login>               - Set global streamer override");
    Serial.println(  "     | channel clear                 - Clear override (auto-find)");
    Serial.println(  "  6  | account list                  - List multi-account profiles");
    Serial.println(  "     | account switch <idx>          - Switch active account profile");
    Serial.println(  "     | account add <name> <token>    - Add account profile");
    Serial.println(  "     | account remove <idx>          - Remove account profile");
    Serial.println(  "     | account rotate on/off         - Toggle automatic account rotation");
    Serial.println(  "  7  | points on/off                 - Toggle auto-claiming channel points");
    Serial.println(  "  8  | led on/off                    - Toggle WS2812 RGB LED indicator");
    Serial.println(  "  9  | scan                          - Scan nearby Wi-Fi networks");
    Serial.println(  " 10  | start / stop                  - Enable / Disable farming loop");
    Serial.println(  " 11  | claim                         - Force inventory refresh & claim");
    Serial.println(  " 12  | reset                         - Clear NVS settings to default");
    Serial.println(  " 13  | reboot                        - Restart ESP32-S3");
    Serial.println(  "=======================================================\n");
}

static const char* gameStatusStr(GameStatus s) {
    switch (s) {
        case GAME_QUEUED:          return "QUEUED";
        case GAME_FARMING:         return "FARMING";
        case GAME_COMPLETED:       return "COMPLETED";
        case GAME_AUTO_DISCOVERED: return "AUTO";
        default:                   return "?";
    }
}

void CLIMenu::printStatus() {
    Serial.println("\n--- [ESP32-S3 FARMER STATUS] ---");
    Serial.printf("Firmware Version:   %s\n", FIRMWARE_VERSION);
    Serial.printf("Wi-Fi Mode:         %s (IP: %s)\n", g_state.wifi_connected ? "STA Connected" : "AP Mode (192.168.4.1)", g_state.current_ip);
    Serial.printf("Active Account:     [%u] %s (ID: %s, User: %s)\n", 
        g_config.active_account_idx + 1,
        (g_config.active_account_idx < g_config.account_count) ? g_config.accounts[g_config.active_account_idx].name : "Default",
        strlen(g_state.user_id) > 0 ? g_state.user_id : "--",
        strlen(g_state.user_login) > 0 ? g_state.user_login : "<not fetched>");
    Serial.printf("OAuth Token:        %s\n", TwitchAPI::getCleanToken().length() > 0 ? "***** (Configured)" : "NOT SET!");
    Serial.printf("PubSub WebSocket:   %s\n", g_state.pubsub_connected ? "CONNECTED (WSS)" : "Disconnected");
    Serial.printf("Active Game:        %s\n", g_config.target_game[0] ? g_config.target_game : "<auto>");
    Serial.printf("Target Streamer:    %s (ID: %s)\n", strlen(g_state.current_channel) > 0 ? g_state.current_channel : "<searching>", strlen(g_state.current_channel_id) > 0 ? g_state.current_channel_id : "--");
    Serial.printf("Pinned Channel:     %s\n", g_config.target_channel[0] ? g_config.target_channel : "<auto-find>");
    Serial.printf("Active Campaign:    %s\n", g_state.active_drop_name);
    Serial.printf("Drop Progress:      %u%%\n", g_state.drop_progress_pct);
    Serial.printf("Total Farmed:       %u minutes watched\n", g_state.total_minutes_watched);
    Serial.printf("Total Claimed:      %u drops | %u channel points\n", g_state.drops_claimed_count, g_state.channel_points_claimed_count);
    Serial.printf("Heartbeat Interval: %u ms (Jitter: 56-68s)\n", g_state.current_heartbeat_interval_ms);
    Serial.printf("RGB LED:            %s\n", g_config.led_enabled ? "ENABLED (@ 10%)" : "OFF");
    Serial.printf("Auto-Points Claim:  %s\n", g_config.auto_claim_points ? "ENABLED" : "OFF");
    Serial.printf("Free Heap RAM:      %u bytes | Free PSRAM: %u bytes\n", ESP.getFreeHeap(), ESP.getFreePsram());
    Serial.printf("Status Msg:         %s\n", g_state.status_message);

    // Print game priority queue
    Serial.println("\n--- [PRIORITY GAME QUEUE] ---");
    if (g_config.game_queue_count == 0) {
        Serial.println("  (empty — will auto-discover campaigns)");
    } else {
        Serial.println("  #  | Priority | Status     | Progress | Game (Preferred Streamers)");
        Serial.println("  ---|----------|------------|----------|----------------------------------------");
        for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
            const GameEntry& e = g_config.game_queue[i];
            String stList = "";
            for (uint8_t s = 0; s < e.streamer_count; s++) {
                if (s > 0) stList += ", ";
                stList += e.preferred_streamers[s];
            }
            Serial.printf("  %-2d | %-8d | %-10s | %3d%%     | %s %s\n",
                i + 1, e.priority, gameStatusStr(e.status), e.progress_pct, e.name,
                stList.length() > 0 ? (String(" [") + stList + "]").c_str() : "");
        }
    }
    Serial.println("------------------------------------------------------------------------\n");
}

void CLIMenu::processInput() {
    if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.length() > 0) {
            handleCommand(input);
        }
    }
}

static int findGameInQueue(const char* name) {
    for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
        if (strcasestr(g_config.game_queue[i].name, name) != NULL) {
            return i;
        }
    }
    return -1;
}

void CLIMenu::handleCommand(const String& cmd) {
    if (cmd == "1" || cmd == "status") {
        printStatus();
    } else if (cmd.startsWith("2 ") || cmd.startsWith("wifi ")) {
        int firstSpace = cmd.indexOf(' ');
        int secondSpace = cmd.indexOf(' ', firstSpace + 1);
        if (firstSpace < 0 || secondSpace < 0) {
            Serial.println("Usage: wifi <SSID> <PASSWORD>");
            return;
        }
        String ssid = cmd.substring(firstSpace + 1, secondSpace);
        String pass = cmd.substring(secondSpace + 1);
        ssid.trim();
        pass.trim();
        strncpy(g_config.wifi_ssid, ssid.c_str(), sizeof(g_config.wifi_ssid));
        strncpy(g_config.wifi_pass, pass.c_str(), sizeof(g_config.wifi_pass));
        StorageManager::saveConfig(g_config);
        Logger::info("Wi-Fi credentials updated. Connecting...");
        WiFiManager::connectSTA(g_config.wifi_ssid, g_config.wifi_pass);
    } else if (cmd.startsWith("3 ") || cmd.startsWith("token ")) {
        String token = cmd.substring(cmd.indexOf(' ') + 1);
        token.trim();
        if (token.startsWith("oauth:")) token = token.substring(6);
        strncpy(g_config.oauth_token, token.c_str(), sizeof(g_config.oauth_token));
        if (g_config.active_account_idx < g_config.account_count) {
            strncpy(g_config.accounts[g_config.active_account_idx].oauth_token, token.c_str(), sizeof(g_config.accounts[0].oauth_token));
        }
        StorageManager::saveConfig(g_config);
        Logger::info("OAuth Token saved. Re-authenticating...");
        TwitchAPI::fetchIntegrityToken();
        TwitchAPI::fetchCurrentUser();
    } else if (cmd.startsWith("game streamer add ")) {
        // game streamer add <game> <streamer>
        String rest = cmd.substring(18);
        rest.trim();
        int lastSpace = rest.lastIndexOf(' ');
        if (lastSpace < 0) {
            Serial.println("Usage: game streamer add <game_name> <streamer_login>");
            return;
        }
        String gameName = rest.substring(0, lastSpace);
        String streamer = rest.substring(lastSpace + 1);
        gameName.trim();
        streamer.trim();
        int idx = findGameInQueue(gameName.c_str());
        if (idx < 0) {
            Serial.printf("Game '%s' not found in queue.\n", gameName.c_str());
            return;
        }
        GameEntry& entry = g_config.game_queue[idx];
        if (entry.streamer_count >= MAX_PREFERRED_STREAMERS) {
            Serial.printf("Game '%s' already has max %d streamers.\n", entry.name, MAX_PREFERRED_STREAMERS);
            return;
        }
        strncpy(entry.preferred_streamers[entry.streamer_count], streamer.c_str(), sizeof(entry.preferred_streamers[0]) - 1);
        entry.preferred_streamers[entry.streamer_count][sizeof(entry.preferred_streamers[0]) - 1] = '\0';
        entry.streamer_count++;
        StorageManager::saveConfig(g_config);
        Logger::info("Added preferred streamer '%s' for game '%s'", streamer.c_str(), entry.name);
    } else if (cmd.startsWith("game streamer remove ")) {
        String rest = cmd.substring(21);
        rest.trim();
        int lastSpace = rest.lastIndexOf(' ');
        if (lastSpace < 0) {
            Serial.println("Usage: game streamer remove <game_name> <streamer_login>");
            return;
        }
        String gameName = rest.substring(0, lastSpace);
        String streamer = rest.substring(lastSpace + 1);
        gameName.trim();
        streamer.trim();
        int idx = findGameInQueue(gameName.c_str());
        if (idx < 0) {
            Serial.printf("Game '%s' not found in queue.\n", gameName.c_str());
            return;
        }
        GameEntry& entry = g_config.game_queue[idx];
        int sIdx = -1;
        for (uint8_t s = 0; s < entry.streamer_count; s++) {
            if (strcasecmp(entry.preferred_streamers[s], streamer.c_str()) == 0) {
                sIdx = s;
                break;
            }
        }
        if (sIdx >= 0) {
            for (uint8_t s = sIdx; s < entry.streamer_count - 1; s++) {
                strncpy(entry.preferred_streamers[s], entry.preferred_streamers[s + 1], sizeof(entry.preferred_streamers[0]));
            }
            entry.streamer_count--;
            StorageManager::saveConfig(g_config);
            Logger::info("Removed streamer '%s' from game '%s'", streamer.c_str(), entry.name);
        } else {
            Serial.printf("Streamer '%s' not found for game '%s'.\n", streamer.c_str(), entry.name);
        }
    } else if (cmd.startsWith("game streamer clear ")) {
        String gameName = cmd.substring(20);
        gameName.trim();
        int idx = findGameInQueue(gameName.c_str());
        if (idx < 0) {
            Serial.printf("Game '%s' not found in queue.\n", gameName.c_str());
            return;
        }
        g_config.game_queue[idx].streamer_count = 0;
        StorageManager::saveConfig(g_config);
        Logger::info("Cleared preferred streamers for game '%s'", g_config.game_queue[idx].name);
    } else if (cmd.startsWith("game add ")) {
        String rest = cmd.substring(9);
        rest.trim();
        int targetPrio = 0;
        String gameName = rest;
        int lastSpace = rest.lastIndexOf(' ');
        if (lastSpace > 0) {
            int parsed = rest.substring(lastSpace + 1).toInt();
            if (parsed > 0) {
                gameName = rest.substring(0, lastSpace);
                gameName.trim();
                targetPrio = parsed;
            }
        }
        if (findGameInQueue(gameName.c_str()) >= 0) {
            Serial.printf("Game '%s' is already in queue.\n", gameName.c_str());
            return;
        }

        if (g_config.game_queue_count >= MAX_PRIORITY_GAMES) {
            // Check if we can evict a completed or auto-discovered game
            int evictIdx = -1;
            for (int i = g_config.game_queue_count - 1; i >= 0; i--) {
                if (g_config.game_queue[i].status == GAME_COMPLETED || g_config.game_queue[i].status == GAME_AUTO_DISCOVERED) {
                    evictIdx = i;
                    break;
                }
            }
            if (evictIdx >= 0) {
                Logger::info("Queue full: Evicting low-priority/completed '%s' to make room for '%s'", g_config.game_queue[evictIdx].name, gameName.c_str());
                for (uint8_t i = evictIdx; i < g_config.game_queue_count - 1; i++) {
                    g_config.game_queue[i] = g_config.game_queue[i + 1];
                }
                g_config.game_queue_count--;
            } else {
                Serial.printf("Queue full! Maximum %d active user games allowed.\n", MAX_PRIORITY_GAMES);
                return;
            }
        }

        GameEntry& entry = g_config.game_queue[g_config.game_queue_count];
        strncpy(entry.name, gameName.c_str(), sizeof(entry.name) - 1);
        entry.name[sizeof(entry.name) - 1] = '\0';
        entry.status = GAME_QUEUED;
        entry.progress_pct = 0;
        entry.minutes_watched = 0;
        entry.streamer_count = 0;
        entry.priority = (targetPrio > 0) ? targetPrio : (g_config.game_queue_count + 1);
        g_config.game_queue_count++;
        StorageManager::saveConfig(g_config);
        Logger::info("Added '%s' to game queue (Priority #%d)", gameName.c_str(), entry.priority);
        TwitchAPI::selectNextGameFromQueue();
    } else if (cmd == "game prune" || cmd == "game clean") {
        uint8_t removed = 0;
        for (int i = g_config.game_queue_count - 1; i >= 0; i--) {
            if (g_config.game_queue[i].status == GAME_COMPLETED || g_config.game_queue[i].status == GAME_AUTO_DISCOVERED) {
                for (uint8_t j = i; j < g_config.game_queue_count - 1; j++) {
                    g_config.game_queue[j] = g_config.game_queue[j + 1];
                }
                g_config.game_queue_count--;
                removed++;
            }
        }
        for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
            g_config.game_queue[i].priority = i + 1;
        }
        StorageManager::saveConfig(g_config);
        Logger::info("Pruned %u completed/auto-discovered games from queue. %u active games remain.", removed, g_config.game_queue_count);
    } else if (cmd.startsWith("game remove ")) {
        String gameName = cmd.substring(12);
        gameName.trim();
        int idx = findGameInQueue(gameName.c_str());
        if (idx < 0) {
            Serial.printf("Game '%s' not found in queue.\n", gameName.c_str());
            return;
        }
        bool wasActive = (strcmp(g_config.game_queue[idx].name, g_config.target_game) == 0);
        for (uint8_t i = idx; i < g_config.game_queue_count - 1; i++) {
            g_config.game_queue[i] = g_config.game_queue[i + 1];
        }
        g_config.game_queue_count--;
        StorageManager::saveConfig(g_config);
        Logger::info("Removed '%s' from game queue", gameName.c_str());
        if (wasActive) {
            TwitchAPI::selectNextGameFromQueue();
        }
    } else if (cmd == "game list") {
        printStatus();
    } else if (cmd == "game clear") {
        g_config.game_queue_count = 0;
        g_config.target_game[0] = '\0';
        g_state.current_channel[0] = '\0';
        g_state.current_channel_id[0] = '\0';
        g_state.current_broadcast_id[0] = '\0';
        StorageManager::saveConfig(g_config);
        Logger::info("Game queue cleared.");
    } else if (cmd == "account list") {
        Serial.println("\n--- [TWITCH ACCOUNT PROFILES] ---");
        if (g_config.account_count == 0) {
            Serial.println("  (no profiles configured)");
        } else {
            for (uint8_t i = 0; i < g_config.account_count; i++) {
                const AccountProfile& a = g_config.accounts[i];
                Serial.printf("  [%u] %s %s | User: %s (ID: %s) | Farmed: %u min | Drops: %u | Pts: %u\n",
                    i + 1,
                    (i == g_config.active_account_idx) ? "👉 [ACTIVE]" : "          ",
                    a.name,
                    a.user_login[0] ? a.user_login : "<not fetched>",
                    a.user_id[0] ? a.user_id : "--",
                    a.total_minutes,
                    a.drops_claimed,
                    a.points_claimed);
            }
        }
        Serial.printf("Auto-Rotation: %s\n", g_config.account_rotation_enabled ? "ENABLED" : "DISABLED");
        Serial.println("--------------------------------\n");
    } else if (cmd.startsWith("account switch ")) {
        int idx = cmd.substring(15).toInt() - 1;
        if (idx >= 0 && idx < g_config.account_count) {
            TwitchAPI::switchAccount(idx);
        } else {
            Serial.printf("Invalid account index. Use 'account list' to see available profiles.\n");
        }
    } else if (cmd.startsWith("account add ")) {
        // account add <name> <oauth_token>
        String rest = cmd.substring(12);
        rest.trim();
        int sp = rest.indexOf(' ');
        if (sp < 0) {
            Serial.println("Usage: account add <ProfileName> <OAuthToken>");
            return;
        }
        String name = rest.substring(0, sp);
        String token = rest.substring(sp + 1);
        name.trim();
        token.trim();
        if (token.startsWith("oauth:")) token = token.substring(6);
        if (g_config.account_count >= MAX_ACCOUNTS) {
            Serial.printf("Max %d account profiles reached.\n", MAX_ACCOUNTS);
            return;
        }
        AccountProfile& a = g_config.accounts[g_config.account_count];
        strncpy(a.name, name.c_str(), sizeof(a.name) - 1);
        strncpy(a.oauth_token, token.c_str(), sizeof(a.oauth_token) - 1);
        a.enabled = true;
        a.total_minutes = 0;
        a.drops_claimed = 0;
        a.points_claimed = 0;
        g_config.account_count++;
        StorageManager::saveConfig(g_config);
        Logger::info("Added account profile [%u]: '%s'", g_config.account_count, name.c_str());
    } else if (cmd.startsWith("account remove ")) {
        int idx = cmd.substring(15).toInt() - 1;
        if (idx >= 0 && idx < g_config.account_count) {
            for (uint8_t i = idx; i < g_config.account_count - 1; i++) {
                g_config.accounts[i] = g_config.accounts[i + 1];
            }
            g_config.account_count--;
            if (g_config.active_account_idx >= g_config.account_count && g_config.account_count > 0) {
                g_config.active_account_idx = 0;
            }
            StorageManager::saveConfig(g_config);
            Logger::info("Removed account profile [%d]", idx + 1);
        } else {
            Serial.println("Invalid account index.");
        }
    } else if (cmd == "account rotate on") {
        g_config.account_rotation_enabled = true;
        StorageManager::saveConfig(g_config);
        Logger::info("Multi-Account Auto-Rotation ENABLED.");
    } else if (cmd == "account rotate off") {
        g_config.account_rotation_enabled = false;
        StorageManager::saveConfig(g_config);
        Logger::info("Multi-Account Auto-Rotation DISABLED.");
    } else if (cmd == "points on") {
        g_config.auto_claim_points = true;
        StorageManager::saveConfig(g_config);
        Logger::info("Channel Points Auto-Claim ENABLED.");
    } else if (cmd == "points off") {
        g_config.auto_claim_points = false;
        StorageManager::saveConfig(g_config);
        Logger::info("Channel Points Auto-Claim DISABLED.");
    } else if (cmd == "led on") {
        g_config.led_enabled = true;
        LedIndicator::setEnabled(true);
        StorageManager::saveConfig(g_config);
        Logger::info("WS2812 RGB LED Indicator ENABLED.");
    } else if (cmd == "led off") {
        g_config.led_enabled = false;
        LedIndicator::setEnabled(false);
        StorageManager::saveConfig(g_config);
        Logger::info("WS2812 RGB LED Indicator DISABLED.");
    } else if (cmd == "channel clear") {
        g_config.target_channel[0] = '\0';
        g_state.current_channel[0] = '\0';
        g_state.current_channel_id[0] = '\0';
        g_state.current_broadcast_id[0] = '\0';
        StorageManager::saveConfig(g_config);
        Logger::info("Streamer channel cleared. Auto-find mode enabled.");
    } else if (cmd.startsWith("channel ")) {
        String channel = cmd.substring(8);
        channel.trim();
        snprintf(g_config.target_channel, sizeof(g_config.target_channel), "%s", channel.c_str());
        StorageManager::saveConfig(g_config);
        Logger::info("Target streamer channel set to: %s", g_config.target_channel);
    } else if (cmd == "scan") {
        WiFiManager::scanNetworksJson();
    } else if (cmd == "start") {
        g_config.farming_enabled = true;
        StorageManager::saveConfig(g_config);
        Logger::info("Farming loop STARTED.");
    } else if (cmd == "stop") {
        g_config.farming_enabled = false;
        StorageManager::saveConfig(g_config);
        Logger::info("Farming loop STOPPED.");
    } else if (cmd == "claim") {
        Logger::info("Manual drop claim triggered...");
        TwitchAPI::fetchInventoryAndProgress();
    } else if (cmd == "reset") {
        StorageManager::resetConfig();
        Logger::info("NVS reset. Restarting board...");
        delay(1000);
        ESP.restart();
    } else if (cmd == "reboot" || cmd == "restart") {
        Logger::info("Restarting ESP32-S3...");
        delay(500);
        ESP.restart();
    } else if (cmd == "help" || cmd == "?") {
        printBanner();
    } else {
        Serial.printf("Unknown command: '%s'. Type 'help' for command list.\n", cmd.c_str());
    }
}
