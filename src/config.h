#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Software Version
#define FIRMWARE_VERSION "1.1.0"

// Default AP Configuration
#define DEFAULT_AP_SSID "ESP32-Twitch-Farmer"
#define DEFAULT_AP_PASS "12345678"

// Default Twitch Client ID (Standard Web Client)
#define DEFAULT_CLIENT_ID "kimne78kx3ncx6brgo4mv6wki5h1ko"

// Priority Game Queue Constants
#define MAX_PRIORITY_GAMES 8

// Game Queue Entry Status
enum GameStatus : uint8_t {
    GAME_QUEUED         = 0,
    GAME_FARMING        = 1,
    GAME_COMPLETED      = 2,
    GAME_AUTO_DISCOVERED = 3
};

// Single game entry in the priority queue
struct GameEntry {
    char name[64];
    uint8_t priority;          // 1 = highest, 255 = lowest
    GameStatus status;
    uint8_t progress_pct;      // Last known progress %
    uint16_t minutes_watched;  // Minutes accumulated on this game
};

// System Configuration Struct
struct AppConfig {
    char wifi_ssid[64];
    char wifi_pass[64];
    char oauth_token[128];
    char client_id[64];
    char target_game[64];       // Currently active game (selected from queue)
    char target_channel[64];
    bool auto_claim;
    uint16_t check_interval_sec;
    bool farming_enabled;
    // Priority game queue
    GameEntry game_queue[MAX_PRIORITY_GAMES];
    uint8_t game_queue_count;
};

// Runtime Farmer State Struct
struct FarmerState {
    bool wifi_connected;
    char current_ip[32];
    bool farming_active;
    bool pubsub_connected;
    char user_id[32];
    char user_login[64];
    char device_id[64];
    char current_channel[64];
    char current_channel_id[32];
    char current_broadcast_id[32];
    char current_game[64];
    char current_game_id[32];
    char active_drop_name[128];
    char drop_instance_id[128];
    uint8_t drop_progress_pct;
    uint32_t total_minutes_watched;
    uint16_t drops_claimed_count;
    char status_message[128];
    uint32_t last_heartbeat_time;
};

extern AppConfig g_config;
extern FarmerState g_state;

#endif // CONFIG_H
