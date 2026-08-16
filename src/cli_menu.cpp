#include "cli_menu.h"
#include "logger.h"
#include "storage.h"
#include "wifi_mgr.h"
#include "twitch_api.h"

void CLIMenu::init() {
    printBanner();
}

void CLIMenu::printBanner() {
    Serial.println("\n=======================================================");
    Serial.printf(  "   ESP32-S3 Twitch Drops Farmer v%s\n", FIRMWARE_VERSION);
    Serial.println(  "   Target MCU: ESP32-S3-WROOM-1 (N16R8)");
    Serial.println(  "=======================================================");
    Serial.println(  " Available USB Commands:");
    Serial.println(  "  1  | status              - Display current status & stats");
    Serial.println(  "  2  | wifi <ssid> <pass>  - Configure Wi-Fi network");
    Serial.println(  "  3  | token <oauth_token> - Set Twitch OAuth token");
    Serial.println(  "  4  | game add <name> [prio]      - Add game to queue (optional priority #)");
    Serial.println(  "     | game priority <name> <val>  - Set exact priority (e.g. game prio Rust 1)");
    Serial.println(  "     | game up <name> [steps]      - Move game up by N steps (default: 1)");
    Serial.println(  "     | game down <name> [steps]    - Move game down by N steps (default: 1)");
    Serial.println(  "     | game remove <name>          - Remove game from queue");
    Serial.println(  "     | game list                   - Show priority queue");
    Serial.println(  "     | game clear                  - Clear all games from queue");
    Serial.println(  "  5  | channel <login>     - Set specific streamer");
    Serial.println(  "     | channel clear       - Clear streamer (auto-find)");
    Serial.println(  "  6  | scan                - Scan nearby Wi-Fi networks");
    Serial.println(  "  7  | start / stop        - Enable / Disable farming loop");
    Serial.println(  "  8  | claim               - Force inventory refresh & claim");
    Serial.println(  "  9  | reset               - Clear NVS settings to default");
    Serial.println(  " 10  | reboot              - Restart ESP32-S3");
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
    Serial.printf("OAuth Token:        %s\n", TwitchAPI::getCleanToken().length() > 0 ? "***** (Configured)" : "NOT SET!");
    Serial.printf("Twitch User:        %s (ID: %s)\n", strlen(g_state.user_login) > 0 ? g_state.user_login : "<not fetched>", strlen(g_state.user_id) > 0 ? g_state.user_id : "--");
    Serial.printf("PubSub WebSocket:   %s\n", g_state.pubsub_connected ? "CONNECTED (WSS)" : "Disconnected");
    Serial.printf("Active Game:        %s\n", g_config.target_game[0] ? g_config.target_game : "<auto>");
    Serial.printf("Target Streamer:    %s (ID: %s)\n", strlen(g_state.current_channel) > 0 ? g_state.current_channel : "<searching>", strlen(g_state.current_channel_id) > 0 ? g_state.current_channel_id : "--");
    Serial.printf("Pinned Channel:     %s\n", g_config.target_channel[0] ? g_config.target_channel : "<auto-find>");
    Serial.printf("Active Campaign:    %s\n", g_state.active_drop_name);
    Serial.printf("Drop Progress:      %u%%\n", g_state.drop_progress_pct);
    Serial.printf("Total Farmed:       %u minutes watched\n", g_state.total_minutes_watched);
    Serial.printf("Total Claimed:      %u drops\n", g_state.drops_claimed_count);
    Serial.printf("Free Heap RAM:      %u bytes\n", ESP.getFreeHeap());
    Serial.printf("Free PSRAM:         %u bytes\n", ESP.getFreePsram());
    Serial.printf("Status Msg:         %s\n", g_state.status_message);

    // Print game priority queue
    Serial.println("\n--- [PRIORITY GAME QUEUE] ---");
    if (g_config.game_queue_count == 0) {
        Serial.println("  (empty — will auto-discover campaigns)");
    } else {
        Serial.println("  #  | Priority | Status     | Progress | Game");
        Serial.println("  ---|----------|------------|----------|-------------------");
        for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
            const GameEntry& e = g_config.game_queue[i];
            Serial.printf("  %-2d | %-8d | %-10s | %3d%%     | %s\n",
                i + 1, e.priority, gameStatusStr(e.status), e.progress_pct, e.name);
        }
    }
    Serial.println("--------------------------------\n");
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

// Find game index in queue by name (case-insensitive partial match)
static int findGameInQueue(const char* name) {
    for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
        if (strcasestr(g_config.game_queue[i].name, name) != NULL) {
            return i;
        }
    }
    return -1;
}

void CLIMenu::handleCommand(String cmd) {
    if (cmd == "1" || cmd == "status") {
        printStatus();
    } else if (cmd.startsWith("2 ") || cmd.startsWith("wifi ")) {
        int spaceIndex = cmd.indexOf(' ', 5);
        String ssid = "", pass = "";
        if (spaceIndex != -1) {
            ssid = cmd.substring(cmd.indexOf(' ') + 1, spaceIndex);
            pass = cmd.substring(spaceIndex + 1);
        } else {
            ssid = cmd.substring(cmd.indexOf(' ') + 1);
        }
        snprintf(g_config.wifi_ssid, sizeof(g_config.wifi_ssid), "%s", ssid.c_str());
        snprintf(g_config.wifi_pass, sizeof(g_config.wifi_pass), "%s", pass.c_str());
        StorageManager::saveConfig(g_config);
        Logger::info("Wi-Fi credentials saved via USB. Connecting...");
        WiFiManager::connectSTA(g_config.wifi_ssid, g_config.wifi_pass);
    } else if (cmd.startsWith("3 ") || cmd.startsWith("token ")) {
        String token = cmd.substring(cmd.indexOf(' ') + 1);
        token.trim();
        if (token.startsWith("oauth:")) {
            token = token.substring(6);
        }
        snprintf(g_config.oauth_token, sizeof(g_config.oauth_token), "%s", token.c_str());
        StorageManager::saveConfig(g_config);
        Logger::info("OAuth Token set successfully via USB CDC!");

    // --- Game queue sub-commands ---
    } else if (cmd == "4" || cmd == "game list") {
        // Print queue table
        if (g_config.game_queue_count == 0) {
            Serial.println("Game queue is empty. Use 'game add <name>' to add games.");
        } else {
            Serial.println("\n  #  | Priority | Status     | Progress | Game");
            Serial.println("  ---|----------|------------|----------|-------------------");
            for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
                const GameEntry& e = g_config.game_queue[i];
                Serial.printf("  %-2d | %-8d | %-10s | %3d%%     | %s\n",
                    i + 1, e.priority, gameStatusStr(e.status), e.progress_pct, e.name);
            }
        }
    } else if (cmd.startsWith("game add ")) {
        String param = cmd.substring(9);
        param.trim();
        if (param.length() == 0) {
            Serial.println("Usage: game add <game_name> [priority]");
            return;
        }
        if (g_config.game_queue_count >= MAX_PRIORITY_GAMES) {
            Serial.printf("Queue full! Maximum %d games allowed.\n", MAX_PRIORITY_GAMES);
            return;
        }

        // Check if optional priority was provided at the end
        String gameName = param;
        int targetPrio = 0;
        int lastSpace = param.lastIndexOf(' ');
        if (lastSpace > 0) {
            int p = param.substring(lastSpace + 1).toInt();
            if (p > 0) {
                gameName = param.substring(0, lastSpace);
                gameName.trim();
                targetPrio = p;
            }
        }

        // Check for duplicate
        if (findGameInQueue(gameName.c_str()) >= 0) {
            Serial.printf("Game '%s' is already in the queue.\n", gameName.c_str());
            return;
        }

        GameEntry& entry = g_config.game_queue[g_config.game_queue_count];
        strncpy(entry.name, gameName.c_str(), sizeof(entry.name) - 1);
        entry.name[sizeof(entry.name) - 1] = '\0';
        entry.status = GAME_QUEUED;
        entry.progress_pct = 0;
        entry.minutes_watched = 0;

        if (targetPrio > 0) {
            if (targetPrio > g_config.game_queue_count + 1) targetPrio = g_config.game_queue_count + 1;
            entry.priority = targetPrio;
            int insertIdx = targetPrio - 1;
            GameEntry temp = entry;
            for (int i = g_config.game_queue_count; i > insertIdx; i--) {
                g_config.game_queue[i] = g_config.game_queue[i - 1];
            }
            g_config.game_queue[insertIdx] = temp;
        } else {
            uint8_t maxPri = 0;
            for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
                if (g_config.game_queue[i].priority > maxPri) maxPri = g_config.game_queue[i].priority;
            }
            entry.priority = maxPri + 1;
        }
        g_config.game_queue_count++;

        // Normalize priorities
        for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
            g_config.game_queue[i].priority = i + 1;
        }

        StorageManager::saveConfig(g_config);
        Logger::info("Added '%s' to game queue (Priority #%d)", gameName.c_str(), targetPrio > 0 ? targetPrio : g_config.game_queue_count);

        if (g_config.game_queue_count == 1 || targetPrio == 1 || g_config.target_game[0] == '\0') {
            TwitchAPI::selectNextGameFromQueue();
        }
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
        for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
            g_config.game_queue[i].priority = i + 1;
        }
        StorageManager::saveConfig(g_config);
        Logger::info("Removed '%s' from game queue", gameName.c_str());
        if (wasActive) {
            TwitchAPI::selectNextGameFromQueue();
        }
    } else if (cmd == "game clear") {
        g_config.game_queue_count = 0;
        g_config.target_game[0] = '\0';
        g_state.current_channel[0] = '\0';
        g_state.current_channel_id[0] = '\0';
        g_state.current_broadcast_id[0] = '\0';
        StorageManager::saveConfig(g_config);
        Logger::info("Game queue cleared. Will auto-discover campaigns on next cycle.");
    } else if (cmd.startsWith("game priority ") || cmd.startsWith("game prio ") || cmd.startsWith("game set ")) {
        int firstSpace = cmd.indexOf(' ');
        int secondSpace = cmd.indexOf(' ', firstSpace + 1);
        String rest = cmd.substring(secondSpace + 1);
        rest.trim();
        int lastSpace = rest.lastIndexOf(' ');
        if (lastSpace < 0) {
            Serial.println("Usage: game priority <game_name> <priority_number>");
            return;
        }
        String gameName = rest.substring(0, lastSpace);
        gameName.trim();
        int targetPrio = rest.substring(lastSpace + 1).toInt();
        int idx = findGameInQueue(gameName.c_str());
        if (idx < 0) {
            Serial.printf("Game '%s' not found in queue.\n", gameName.c_str());
            return;
        }
        if (targetPrio < 1) targetPrio = 1;
        if (targetPrio > g_config.game_queue_count) targetPrio = g_config.game_queue_count;
        int targetIdx = targetPrio - 1;
        if (targetIdx != idx) {
            GameEntry moving = g_config.game_queue[idx];
            if (targetIdx < idx) {
                for (int i = idx; i > targetIdx; i--) {
                    g_config.game_queue[i] = g_config.game_queue[i - 1];
                }
            } else {
                for (int i = idx; i < targetIdx; i++) {
                    g_config.game_queue[i] = g_config.game_queue[i + 1];
                }
            }
            g_config.game_queue[targetIdx] = moving;
            for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
                g_config.game_queue[i].priority = i + 1;
            }
            StorageManager::saveConfig(g_config);
            Logger::info("Set priority of '%s' to #%d", g_config.game_queue[targetIdx].name, targetPrio);
            TwitchAPI::selectNextGameFromQueue();
        } else {
            Serial.printf("Game '%s' is already at priority #%d.\n", gameName.c_str(), targetPrio);
        }
    } else if (cmd.startsWith("game up ")) {
        String rest = cmd.substring(8);
        rest.trim();
        int steps = 1;
        String gameName = rest;
        int lastSpace = rest.lastIndexOf(' ');
        if (lastSpace > 0) {
            int parsedSteps = rest.substring(lastSpace + 1).toInt();
            if (parsedSteps > 0) {
                String candidate = rest.substring(0, lastSpace);
                candidate.trim();
                if (findGameInQueue(candidate.c_str()) >= 0) {
                    gameName = candidate;
                    steps = parsedSteps;
                }
            }
        }
        int idx = findGameInQueue(gameName.c_str());
        if (idx < 0) {
            Serial.printf("Game '%s' not found in queue.\n", gameName.c_str());
            return;
        }
        if (idx == 0) {
            Serial.println("Game is already at highest priority.");
            return;
        }
        int targetIdx = idx - steps;
        if (targetIdx < 0) targetIdx = 0;
        GameEntry moving = g_config.game_queue[idx];
        for (int i = idx; i > targetIdx; i--) {
            g_config.game_queue[i] = g_config.game_queue[i - 1];
        }
        g_config.game_queue[targetIdx] = moving;
        for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
            g_config.game_queue[i].priority = i + 1;
        }
        StorageManager::saveConfig(g_config);
        Logger::info("Moved '%s' up by %d step(s) to Priority #%d", gameName.c_str(), steps, targetIdx + 1);
        TwitchAPI::selectNextGameFromQueue();
    } else if (cmd.startsWith("game down ")) {
        String rest = cmd.substring(10);
        rest.trim();
        int steps = 1;
        String gameName = rest;
        int lastSpace = rest.lastIndexOf(' ');
        if (lastSpace > 0) {
            int parsedSteps = rest.substring(lastSpace + 1).toInt();
            if (parsedSteps > 0) {
                String candidate = rest.substring(0, lastSpace);
                candidate.trim();
                if (findGameInQueue(candidate.c_str()) >= 0) {
                    gameName = candidate;
                    steps = parsedSteps;
                }
            }
        }
        int idx = findGameInQueue(gameName.c_str());
        if (idx < 0) {
            Serial.printf("Game '%s' not found in queue.\n", gameName.c_str());
            return;
        }
        if (idx >= g_config.game_queue_count - 1) {
            Serial.println("Game is already at lowest priority.");
            return;
        }
        int targetIdx = idx + steps;
        if (targetIdx >= g_config.game_queue_count) targetIdx = g_config.game_queue_count - 1;
        GameEntry moving = g_config.game_queue[idx];
        for (int i = idx; i < targetIdx; i++) {
            g_config.game_queue[i] = g_config.game_queue[i + 1];
        }
        g_config.game_queue[targetIdx] = moving;
        for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
            g_config.game_queue[i].priority = i + 1;
        }
        StorageManager::saveConfig(g_config);
        Logger::info("Moved '%s' down by %d step(s) to Priority #%d", gameName.c_str(), steps, targetIdx + 1);
        TwitchAPI::selectNextGameFromQueue();
    } else if (cmd.startsWith("4 ") || (cmd.startsWith("game ") && !cmd.startsWith("game add") && !cmd.startsWith("game remove") && !cmd.startsWith("game up") && !cmd.startsWith("game down") && !cmd.startsWith("game priority") && !cmd.startsWith("game prio") && !cmd.startsWith("game set") && cmd != "game clear" && cmd != "game list")) {
        // Legacy single game set: "game Rust" — replaces entire queue with one game
        String game = cmd.substring(cmd.indexOf(' ') + 1);
        game.trim();
        if (game.length() == 0) {
            Serial.println("Usage: game add <name>, game remove <name>, game list, game clear, game up/down <name>");
            return;
        }
        // Clear queue and add as single entry
        g_config.game_queue_count = 0;
        GameEntry& entry = g_config.game_queue[0];
        strncpy(entry.name, game.c_str(), sizeof(entry.name) - 1);
        entry.name[sizeof(entry.name) - 1] = '\0';
        entry.priority = 1;
        entry.status = GAME_QUEUED;
        entry.progress_pct = 0;
        entry.minutes_watched = 0;
        g_config.game_queue_count = 1;
        snprintf(g_config.target_game, sizeof(g_config.target_game), "%s", game.c_str());
        g_state.current_channel[0] = '\0';
        g_state.current_channel_id[0] = '\0';
        g_state.current_broadcast_id[0] = '\0';
        StorageManager::saveConfig(g_config);
        Logger::info("Target game set to: %s (queue replaced)", g_config.target_game);
    } else if (cmd == "channel clear" || cmd == "5 clear") {
        g_config.target_channel[0] = '\0';
        g_state.current_channel[0] = '\0';
        g_state.current_channel_id[0] = '\0';
        g_state.current_broadcast_id[0] = '\0';
        StorageManager::saveConfig(g_config);
        Logger::info("Streamer channel cleared. Auto-find mode enabled.");
    } else if (cmd.startsWith("5 ") || cmd.startsWith("channel ")) {
        String channel = cmd.substring(cmd.indexOf(' ') + 1);
        channel.trim();
        if (channel.length() == 0 || channel == "clear") {
            g_config.target_channel[0] = '\0';
            g_state.current_channel[0] = '\0';
            g_state.current_channel_id[0] = '\0';
            g_state.current_broadcast_id[0] = '\0';
            StorageManager::saveConfig(g_config);
            Logger::info("Streamer channel cleared. Auto-find mode enabled.");
        } else {
            snprintf(g_config.target_channel, sizeof(g_config.target_channel), "%s", channel.c_str());
            StorageManager::saveConfig(g_config);
            Logger::info("Target streamer channel set to: %s", g_config.target_channel);
        }
    } else if (cmd == "6" || cmd == "scan") {
        WiFiManager::scanNetworksJson();
    } else if (cmd == "7" || cmd == "start") {
        g_config.farming_enabled = true;
        StorageManager::saveConfig(g_config);
        Logger::info("Farming loop STARTED.");
    } else if (cmd == "stop") {
        g_config.farming_enabled = false;
        StorageManager::saveConfig(g_config);
        Logger::info("Farming loop STOPPED.");
    } else if (cmd == "8" || cmd == "claim") {
        Logger::info("Manual drop claim triggered...");
        TwitchAPI::fetchInventoryAndProgress();
    } else if (cmd == "9" || cmd == "reset") {
        StorageManager::resetConfig();
        Logger::info("NVS reset. Restarting board...");
        delay(1000);
        ESP.restart();
    } else if (cmd == "10" || cmd == "reboot" || cmd == "restart") {
        Logger::info("Restarting ESP32-S3...");
        delay(500);
        ESP.restart();
    } else if (cmd == "help" || cmd == "?") {
        printBanner();
    } else {
        Serial.printf("Unknown command: '%s'. Type 'help' or '1' for status.\n", cmd.c_str());
    }
}
