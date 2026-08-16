#include "web_server.h"
#include "logger.h"
#include "storage.h"
#include "wifi_mgr.h"
#include "twitch_api.h"
#include <ArduinoJson.h>

WebServer WebServerManager::server(80);

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32-S3 Twitch Drops Farmer</title>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;600;700&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-dark: #0f172a;
            --card-bg: rgba(30, 41, 59, 0.7);
            --border-color: rgba(255, 255, 255, 0.1);
            --primary: #9146ff;
            --primary-glow: rgba(145, 70, 255, 0.4);
            --accent: #06b6d4;
            --success: #10b981;
            --warning: #f59e0b;
            --danger: #ef4444;
            --text-main: #f8fafc;
            --text-muted: #94a3b8;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; font-family: 'Inter', sans-serif; }
        body {
            background-color: var(--bg-dark);
            color: var(--text-main);
            padding: 20px;
            min-height: 100vh;
            background-image: radial-gradient(circle at 10% 20%, rgba(145, 70, 255, 0.15) 0%, transparent 40%),
                              radial-gradient(circle at 90% 80%, rgba(6, 182, 212, 0.15) 0%, transparent 40%);
        }
        .container { max-width: 1000px; margin: 0 auto; }
        header {
            display: flex; justify-content: space-between; align-items: center;
            padding: 20px; background: var(--card-bg); backdrop-filter: blur(12px);
            border: 1px solid var(--border-color); border-radius: 16px; margin-bottom: 24px;
        }
        .logo { display: flex; align-items: center; gap: 12px; }
        .logo svg { fill: var(--primary); width: 32px; height: 32px; filter: drop-shadow(0 0 8px var(--primary-glow)); }
        .logo h1 { font-size: 1.25rem; font-weight: 700; background: linear-gradient(135deg, #fff, #94a3b8); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }
        .status-badge {
            padding: 6px 14px; border-radius: 20px; font-size: 0.85rem; font-weight: 600;
            background: rgba(16, 185, 129, 0.15); color: var(--success); border: 1px solid rgba(16, 185, 129, 0.3);
        }
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; margin-bottom: 24px; }
        .card {
            background: var(--card-bg); backdrop-filter: blur(12px);
            border: 1px solid var(--border-color); border-radius: 16px; padding: 20px;
        }
        .card-full { grid-column: 1 / -1; }
        .card h2 { font-size: 1rem; color: var(--text-muted); margin-bottom: 14px; display: flex; justify-content: space-between; align-items: center; }
        .stat-value { font-size: 1.8rem; font-weight: 700; color: #fff; margin: 8px 0; }
        .stat-sub { font-size: 0.85rem; color: var(--accent); }
        .progress-bar-bg { background: rgba(255, 255, 255, 0.1); height: 10px; border-radius: 5px; overflow: hidden; margin-top: 12px; }
        .progress-bar-fill { background: linear-gradient(90deg, var(--primary), var(--accent)); height: 100%; width: 0%; transition: width 0.5s ease; }
        .form-group { margin-bottom: 14px; }
        label { display: block; font-size: 0.85rem; color: var(--text-muted); margin-bottom: 6px; }
        input[type="text"], input[type="password"], select {
            width: 100%; padding: 10px 14px; background: rgba(15, 23, 42, 0.6);
            border: 1px solid var(--border-color); border-radius: 8px; color: #fff; font-size: 0.9rem;
        }
        input:focus, select:focus { outline: none; border-color: var(--primary); box-shadow: 0 0 10px var(--primary-glow); }
        .btn {
            padding: 10px 16px; border: none; border-radius: 8px; font-weight: 600; font-size: 0.85rem;
            background: linear-gradient(135deg, var(--primary), #7928ca); color: #fff; cursor: pointer; transition: transform 0.2s, box-shadow 0.2s;
        }
        .btn-full { width: 100%; padding: 12px; }
        .btn:hover { transform: translateY(-2px); box-shadow: 0 4px 15px var(--primary-glow); }
        .btn-secondary { background: rgba(255, 255, 255, 0.1); color: var(--text-main); }
        .btn-secondary:hover { background: rgba(255, 255, 255, 0.2); box-shadow: none; transform: none; }
        .btn-danger { background: linear-gradient(135deg, #dc2626, #991b1b); }
        .btn-danger:hover { box-shadow: 0 4px 15px rgba(220, 38, 38, 0.4); }
        .btn-sm { padding: 5px 10px; font-size: 0.75rem; border-radius: 6px; }
        .log-box {
            background: #090d16; border: 1px solid var(--border-color); border-radius: 12px;
            padding: 14px; height: 220px; overflow-y: auto; font-family: monospace; font-size: 0.8rem; color: #38bdf8;
        }
        .log-line { margin-bottom: 4px; word-break: break-all; }
        .log-WARN { color: #f59e0b; }
        .log-ERROR { color: #ef4444; }

        /* Game Queue Styles */
        .queue-list { display: flex; flex-direction: column; gap: 8px; }
        .queue-item {
            display: flex; align-items: center; gap: 10px;
            padding: 10px 14px; border-radius: 10px;
            background: rgba(15, 23, 42, 0.5); border: 1px solid var(--border-color);
            transition: border-color 0.3s, background 0.3s;
        }
        .queue-item:hover { border-color: var(--primary); background: rgba(15, 23, 42, 0.8); }
        .queue-item.auto-discovered { border-style: dashed; border-color: rgba(245, 158, 11, 0.3); }
        .queue-item.completed { opacity: 0.5; }
        .queue-num {
            min-width: 28px; height: 28px; border-radius: 50%;
            display: flex; align-items: center; justify-content: center;
            font-size: 0.75rem; font-weight: 700;
            background: var(--primary); color: #fff;
        }
        .queue-item.completed .queue-num { background: var(--success); }
        .queue-item.auto-discovered .queue-num { background: var(--warning); }
        .queue-info { flex: 1; min-width: 0; }
        .queue-name { font-weight: 600; font-size: 0.9rem; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
        .queue-meta { display: flex; gap: 8px; align-items: center; margin-top: 3px; }
        .queue-status {
            font-size: 0.7rem; font-weight: 600; padding: 2px 8px; border-radius: 10px;
            text-transform: uppercase; letter-spacing: 0.5px;
        }
        .status-farming { background: rgba(145, 70, 255, 0.2); color: var(--primary); }
        .status-queued { background: rgba(6, 182, 212, 0.2); color: var(--accent); }
        .status-completed { background: rgba(16, 185, 129, 0.2); color: var(--success); }
        .status-auto { background: rgba(245, 158, 11, 0.2); color: var(--warning); }
        .queue-progress { font-size: 0.75rem; color: var(--text-muted); }
        .queue-progress-bar { width: 60px; height: 4px; background: rgba(255,255,255,0.1); border-radius: 2px; overflow: hidden; display: inline-block; vertical-align: middle; margin-left: 4px; }
        .queue-progress-fill { height: 100%; background: var(--accent); border-radius: 2px; }
        .queue-actions { display: flex; gap: 4px; flex-shrink: 0; }
        .queue-btn {
            width: 28px; height: 28px; border-radius: 6px; border: 1px solid var(--border-color);
            background: rgba(255,255,255,0.05); color: var(--text-muted); cursor: pointer;
            display: flex; align-items: center; justify-content: center; font-size: 0.8rem;
            transition: background 0.2s, color 0.2s;
        }
        .queue-btn:hover { background: rgba(255,255,255,0.15); color: #fff; }
        .queue-btn.remove:hover { background: rgba(220, 38, 38, 0.3); color: var(--danger); }
        .add-game-row { display: flex; gap: 8px; margin-top: 12px; }
        .add-game-row input { flex: 1; }
        .streamer-row { display: flex; align-items: center; gap: 8px; margin-top: 8px; }
        .clear-link { font-size: 0.75rem; color: var(--danger); cursor: pointer; text-decoration: underline; opacity: 0.7; transition: opacity 0.2s; }
        .clear-link:hover { opacity: 1; }
        .queue-empty { text-align: center; padding: 30px 10px; color: var(--text-muted); font-size: 0.85rem; }
        .queue-empty span { font-size: 2rem; display: block; margin-bottom: 8px; }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <div class="logo">
                <svg viewBox="0 0 24 24"><path d="M11.571 4.714h1.715v5.143H11.571zm4.715 0H18v5.143h-1.714zM6 0L1.714 4.286v15.428h5.143V24l4.286-4.286h3.428L22.286 12V0zm14.571 11.143l-3.428 3.428h-3.429l-3 3v-3H6.857V1.714h13.714z"/></svg>
                <h1>ESP32-S3 Twitch Drops Farmer</h1>
            </div>
            <div class="status-badge" id="statusBadge">Farming Active</div>
        </header>

        <div class="grid">
            <div class="card">
                <h2>Active Campaign <span class="stat-sub" id="gameLabel">—</span></h2>
                <div class="stat-value" id="dropName">Loading...</div>
                <div style="display:flex; justify-content:space-between; font-size:0.85rem; color:var(--text-muted);">
                    <span>Progress</span>
                    <span id="progressPct">0%</span>
                </div>
                <div class="progress-bar-bg"><div class="progress-bar-fill" id="progressFill"></div></div>
                <div class="streamer-row" style="margin-top:14px; font-size:0.85rem; color:var(--text-muted);">
                    Streamer: <strong style="color:#fff;" id="streamerName">--</strong>
                    <span class="clear-link" id="clearStreamerBtn" onclick="clearChannel()" style="display:none;">✕ clear</span>
                </div>
            </div>

            <div class="card">
                <h2>Farming Stats</h2>
                <div style="display:flex; justify-content:space-around; margin-top:10px;">
                    <div style="text-align:center;">
                        <div class="stat-value" id="minsWatched">0</div>
                        <div style="font-size:0.8rem; color:var(--text-muted);">Minutes Farmed</div>
                    </div>
                    <div style="text-align:center;">
                        <div class="stat-value" id="dropsClaimed" style="color:var(--accent);">0</div>
                        <div style="font-size:0.8rem; color:var(--text-muted);">Drops Claimed</div>
                    </div>
                </div>
                <div style="margin-top:16px; font-size:0.8rem; color:var(--text-muted); text-align:center;" id="sysMemory">
                    Free Heap: -- | PSRAM: --
                </div>
            </div>
        </div>

        <!-- Priority Game Queue Card -->
        <div class="card card-full" style="margin-bottom:24px;">
            <h2>
                Priority Game Queue
                <div style="display:flex; gap:6px;">
                    <button class="btn btn-sm btn-secondary" onclick="discoverGames()">Auto-Discover</button>
                    <button class="btn btn-sm btn-danger" onclick="clearAllGames()">Clear All</button>
                </div>
            </h2>
            <div class="queue-list" id="queueList">
                <div class="queue-empty"><span>📋</span>Loading queue...</div>
            </div>
            <div class="add-game-row">
                <input type="text" id="newGameName" placeholder="Enter game name (e.g. Rust, Valorant)">
                <button class="btn" onclick="addGame()">Add</button>
            </div>
        </div>

        <div class="grid">
            <div class="card">
                <h2>Configuration Settings</h2>
                <form id="configForm">
                    <div class="form-group">
                        <label>Twitch OAuth Token (auth-token)</label>
                        <input type="password" id="oauthToken" placeholder="Paste your Twitch auth-token here">
                    </div>
                    <div class="form-group">
                        <label>Specific Streamer (Optional)</label>
                        <input type="text" id="targetChannel" placeholder="Leave empty for auto-find top stream">
                    </div>
                    <div class="form-group">
                        <label>Wi-Fi SSID</label>
                        <input type="text" id="wifiSsid" placeholder="Home Wi-Fi Name">
                    </div>
                    <div class="form-group">
                        <label>Wi-Fi Password</label>
                        <input type="password" id="wifiPass" placeholder="Wi-Fi Password">
                    </div>
                    <button type="submit" class="btn btn-full">Save & Update Settings</button>
                    <button type="button" class="btn btn-full btn-secondary" style="margin-top:8px;" onclick="triggerClaim()">Force Manual Drop Claim</button>
                </form>
            </div>

            <div class="card">
                <h2>Live Console Logs</h2>
                <div class="log-box" id="logBox">
                    <div class="log-line">System initialized. Waiting for log stream...</div>
                </div>
            </div>
        </div>
    </div>

    <script>
        // --- Status polling ---
        async function fetchStatus() {
            try {
                const res = await fetch('/api/status');
                const data = await res.json();
                
                document.getElementById('dropName').innerText = data.active_drop || 'No Active Drop';
                document.getElementById('gameLabel').innerText = data.current_game || 'Twitch';
                document.getElementById('streamerName').innerText = data.current_channel || '<auto>';
                document.getElementById('progressPct').innerText = data.drop_progress + '%';
                document.getElementById('progressFill').style.width = data.drop_progress + '%';
                document.getElementById('minsWatched').innerText = data.minutes_watched;
                document.getElementById('dropsClaimed').innerText = data.drops_claimed;
                document.getElementById('sysMemory').innerText = `Free Heap: ${(data.free_heap/1024).toFixed(1)}KB | PSRAM: ${(data.free_psram/1024).toFixed(1)}KB`;
                
                // Show clear streamer button if a channel is pinned
                const clearBtn = document.getElementById('clearStreamerBtn');
                if (data.target_channel && data.target_channel.length > 0) {
                    clearBtn.style.display = 'inline';
                } else {
                    clearBtn.style.display = 'none';
                }

                if (!document.activeElement || document.activeElement.tagName !== 'INPUT') {
                    if (data.wifi_ssid) document.getElementById('wifiSsid').value = data.wifi_ssid;
                    if (data.target_channel) document.getElementById('targetChannel').value = data.target_channel;
                }
            } catch(e) {}
        }

        // --- Game Queue ---
        async function fetchGames() {
            try {
                const res = await fetch('/api/games');
                const games = await res.json();
                const list = document.getElementById('queueList');
                
                if (games.length === 0) {
                    list.innerHTML = '<div class="queue-empty"><span>🎮</span>No games in queue<br>Add games below or use Auto-Discover</div>';
                    return;
                }

                list.innerHTML = games.map((g, i) => {
                    const statusClass = g.status === 1 ? 'status-farming' : g.status === 2 ? 'status-completed' : g.status === 3 ? 'status-auto' : 'status-queued';
                    const statusText = g.status === 1 ? 'Farming' : g.status === 2 ? 'Done' : g.status === 3 ? 'Auto' : 'Queued';
                    const itemClass = g.status === 2 ? 'completed' : g.status === 3 ? 'auto-discovered' : '';
                    return `
                        <div class="queue-item ${itemClass}">
                            <div class="queue-num" onclick="setGamePriority('${esc(g.name)}', ${i + 1}, ${games.length})" title="Click to set custom priority number">#${i + 1}</div>
                            <div class="queue-info">
                                <div class="queue-name">${g.name}</div>
                                <div class="queue-meta">
                                    <span class="queue-status ${statusClass}">${statusText}</span>
                                    <span class="queue-progress">${g.progress}%
                                        <span class="queue-progress-bar"><span class="queue-progress-fill" style="width:${g.progress}%"></span></span>
                                    </span>
                                    <span class="queue-progress">${g.minutes}m</span>
                                </div>
                            </div>
                            <div class="queue-actions">
                                <button class="queue-btn" onclick="moveGame('${esc(g.name)}','top')" title="Promote to Top (Priority #1)">⇪</button>
                                <button class="queue-btn" onclick="moveGame('${esc(g.name)}','up')" title="Move Up">▲</button>
                                <button class="queue-btn" onclick="moveGame('${esc(g.name)}','down')" title="Move Down">▼</button>
                                <button class="queue-btn remove" onclick="removeGame('${esc(g.name)}')" title="Remove">✕</button>
                            </div>
                        </div>`;
                }).join('');
            } catch(e) {}
        }

        function esc(s) { return s.replace(/'/g, "\\'").replace(/"/g, '&quot;'); }

        async function addGame() {
            const name = document.getElementById('newGameName').value.trim();
            if (!name) return;
            await fetch('/api/games/add', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({name})});
            document.getElementById('newGameName').value = '';
            fetchGames();
        }

        async function removeGame(name) {
            await fetch('/api/games/remove', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({name})});
            fetchGames();
        }

        async function moveGame(name, direction) {
            if (direction === 'top') {
                await fetch('/api/games/reorder', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({name, priority: 1})});
            } else {
                await fetch('/api/games/reorder', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({name, direction, steps: 1})});
            }
            fetchGames();
        }

        async function setGamePriority(name, currentPri, totalGames) {
            const val = prompt(`Set priority for "${name}" (1 = highest, max ${totalGames}):`, currentPri);
            if (!val) return;
            const prio = parseInt(val);
            if (isNaN(prio) || prio < 1) return;
            await fetch('/api/games/reorder', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({name, priority: prio})
            });
            fetchGames();
        }

        async function clearAllGames() {
            if (!confirm('Clear all games from queue?')) return;
            await fetch('/api/games/clear', {method:'POST'});
            fetchGames();
        }

        async function discoverGames() {
            const btn = event.target;
            btn.innerText = 'Discovering...';
            btn.disabled = true;
            await fetch('/api/claim', {method:'POST'});
            setTimeout(() => { fetchGames(); btn.innerText = 'Auto-Discover'; btn.disabled = false; }, 3000);
        }

        async function clearChannel() {
            await fetch('/api/channel/clear', {method:'POST'});
            document.getElementById('targetChannel').value = '';
            fetchStatus();
        }

        // --- Logs ---
        async function fetchLogs() {
            try {
                const res = await fetch('/api/logs');
                const logs = await res.json();
                const box = document.getElementById('logBox');
                box.innerHTML = logs.map(l => `<div class="log-line log-${l.level}">[${l.level}] ${l.msg}</div>`).join('');
                box.scrollTop = box.scrollHeight;
            } catch(e) {}
        }

        // --- Config form ---
        document.getElementById('configForm').addEventListener('submit', async (e) => {
            e.preventDefault();
            const payload = {
                oauth_token: document.getElementById('oauthToken').value,
                target_channel: document.getElementById('targetChannel').value,
                wifi_ssid: document.getElementById('wifiSsid').value,
                wifi_pass: document.getElementById('wifiPass').value
            };
            await fetch('/api/config', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(payload)
            });
            alert('Settings saved to ESP32-S3 NVS!');
        });

        async function triggerClaim() {
            await fetch('/api/claim', {method: 'POST'});
            alert('Drop inventory check triggered!');
        }

        // Enter key to add game
        document.getElementById('newGameName').addEventListener('keydown', (e) => {
            if (e.key === 'Enter') { e.preventDefault(); addGame(); }
        });

        // Polling intervals
        setInterval(fetchStatus, 3000);
        setInterval(fetchLogs, 2500);
        setInterval(fetchGames, 5000);
        fetchStatus();
        fetchLogs();
        fetchGames();
    </script>
</body>
</html>
)rawliteral";

void WebServerManager::init() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/status", HTTP_GET, handleApiStatus);
    server.on("/api/config", HTTP_POST, handleApiConfig);
    server.on("/api/logs", HTTP_GET, handleApiLogs);
    server.on("/api/wifi-scan", HTTP_GET, handleApiWifiScan);
    server.on("/api/restart", HTTP_POST, handleApiRestart);
    server.on("/api/claim", HTTP_POST, []() {
        TwitchAPI::fetchInventoryAndProgress();
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // Game queue API endpoints
    server.on("/api/games", HTTP_GET, handleApiGames);
    server.on("/api/games/add", HTTP_POST, handleApiGamesAdd);
    server.on("/api/games/remove", HTTP_POST, handleApiGamesRemove);
    server.on("/api/games/reorder", HTTP_POST, handleApiGamesReorder);
    server.on("/api/games/clear", HTTP_POST, handleApiGamesClear);
    server.on("/api/channel/clear", HTTP_POST, handleApiChannelClear);

    server.onNotFound(handleNotFound);
    server.begin();
    Logger::info("Web Server listening on port 80 (http://%s)", WiFiManager::getIP().c_str());
}

void WebServerManager::handleClient() {
    server.handleClient();
}

void WebServerManager::handleRoot() {
    server.send(200, "text/html", INDEX_HTML);
}

void WebServerManager::handleApiStatus() {
    JsonDocument doc;
    doc["version"] = FIRMWARE_VERSION;
    doc["ip"] = g_state.current_ip;
    doc["wifi_connected"] = g_state.wifi_connected;
    doc["pubsub_connected"] = g_state.pubsub_connected;
    doc["user_login"] = g_state.user_login;
    doc["user_id"] = g_state.user_id;
    doc["current_game"] = g_state.current_game[0] ? g_state.current_game : g_config.target_game;
    doc["target_game"] = g_config.target_game;
    doc["target_channel"] = g_config.target_channel;
    doc["current_channel"] = g_state.current_channel;
    doc["active_drop"] = g_state.active_drop_name;
    doc["drop_progress"] = g_state.drop_progress_pct;
    doc["minutes_watched"] = g_state.total_minutes_watched;
    doc["drops_claimed"] = g_state.drops_claimed_count;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["free_psram"] = ESP.getFreePsram();
    doc["wifi_ssid"] = g_config.wifi_ssid;
    doc["queue_count"] = g_config.game_queue_count;

    String jsonStr;
    serializeJson(doc, jsonStr);
    server.send(200, "application/json", jsonStr);
}

void WebServerManager::handleApiConfig() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    String body = server.arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    if (doc["oauth_token"].is<const char*>() && strlen(doc["oauth_token"]) > 0) {
        const char* token = doc["oauth_token"];
        if (strncmp(token, "oauth:", 6) == 0) token += 6;
        snprintf(g_config.oauth_token, sizeof(g_config.oauth_token), "%s", token);
    }
    if (doc["target_channel"].is<const char*>()) {
        snprintf(g_config.target_channel, sizeof(g_config.target_channel), "%s", doc["target_channel"].as<const char*>());
    }
    if (doc["wifi_ssid"].is<const char*>() && strlen(doc["wifi_ssid"]) > 0) {
        snprintf(g_config.wifi_ssid, sizeof(g_config.wifi_ssid), "%s", doc["wifi_ssid"].as<const char*>());
    }
    if (doc["wifi_pass"].is<const char*>() && strlen(doc["wifi_pass"]) > 0) {
        snprintf(g_config.wifi_pass, sizeof(g_config.wifi_pass), "%s", doc["wifi_pass"].as<const char*>());
    }

    StorageManager::saveConfig(g_config);
    Logger::info("Settings updated via Web API!");
    server.send(200, "application/json", "{\"status\":\"saved\"}");

    if (strlen(g_config.wifi_ssid) > 0 && !g_state.wifi_connected) {
        WiFiManager::connectSTA(g_config.wifi_ssid, g_config.wifi_pass);
    }
}

void WebServerManager::handleApiLogs() {
    server.send(200, "application/json", Logger::getLogsJson());
}

void WebServerManager::handleApiWifiScan() {
    server.send(200, "application/json", WiFiManager::scanNetworksJson());
}

void WebServerManager::handleApiRestart() {
    server.send(200, "application/json", "{\"status\":\"restarting\"}");
    delay(500);
    ESP.restart();
}

// ---------------------------------------------------------------------------
// Game Queue API Handlers
// ---------------------------------------------------------------------------

void WebServerManager::handleApiGames() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
        const GameEntry& e = g_config.game_queue[i];
        JsonObject obj = arr.add<JsonObject>();
        obj["name"] = e.name;
        obj["priority"] = e.priority;
        obj["status"] = (uint8_t)e.status;
        obj["progress"] = e.progress_pct;
        obj["minutes"] = e.minutes_watched;
    }

    String jsonStr;
    serializeJson(doc, jsonStr);
    server.send(200, "application/json", jsonStr);
}

void WebServerManager::handleApiGamesAdd() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    const char* name = doc["name"] | "";
    if (strlen(name) == 0) {
        server.send(400, "application/json", "{\"error\":\"Game name required\"}");
        return;
    }

    if (g_config.game_queue_count >= MAX_PRIORITY_GAMES) {
        server.send(400, "application/json", "{\"error\":\"Queue full (max 8)\"}");
        return;
    }

    // Check for duplicates
    for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
        if (strcasestr(g_config.game_queue[i].name, name) != NULL) {
            server.send(400, "application/json", "{\"error\":\"Game already in queue\"}");
            return;
        }
    }

    GameEntry& entry = g_config.game_queue[g_config.game_queue_count];
    strncpy(entry.name, name, sizeof(entry.name) - 1);
    entry.name[sizeof(entry.name) - 1] = '\0';
    uint8_t maxPri = 0;
    for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
        if (g_config.game_queue[i].priority > maxPri) maxPri = g_config.game_queue[i].priority;
    }
    entry.priority = maxPri + 1;
    entry.status = GAME_QUEUED;
    entry.progress_pct = 0;
    entry.minutes_watched = 0;
    g_config.game_queue_count++;

    StorageManager::saveConfig(g_config);
    Logger::info("Added '%s' to game queue via Web (Priority #%d)", entry.name, entry.priority);

    if (g_config.game_queue_count == 1 || g_config.target_game[0] == '\0') {
        TwitchAPI::selectNextGameFromQueue();
    }

    server.send(200, "application/json", "{\"status\":\"added\"}");
}

void WebServerManager::handleApiGamesRemove() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    const char* name = doc["name"] | "";
    if (strlen(name) == 0) {
        server.send(400, "application/json", "{\"error\":\"Game name required\"}");
        return;
    }

    int idx = -1;
    for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
        if (strcasestr(g_config.game_queue[i].name, name) != NULL) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        server.send(404, "application/json", "{\"error\":\"Game not found\"}");
        return;
    }

    bool wasActive = (strcmp(g_config.game_queue[idx].name, g_config.target_game) == 0);
    for (uint8_t i = idx; i < g_config.game_queue_count - 1; i++) {
        g_config.game_queue[i] = g_config.game_queue[i + 1];
    }
    g_config.game_queue_count--;
    StorageManager::saveConfig(g_config);
    Logger::info("Removed '%s' from game queue via Web", name);

    if (wasActive) {
        TwitchAPI::selectNextGameFromQueue();
    }

    server.send(200, "application/json", "{\"status\":\"removed\"}");
}

void WebServerManager::handleApiGamesReorder() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    const char* name = doc["name"] | "";
    const char* direction = doc["direction"] | "";
    int steps = doc["steps"] | 1;
    if (steps < 1) steps = 1;

    int idx = -1;
    for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
        if (strcasestr(g_config.game_queue[i].name, name) != NULL) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        server.send(404, "application/json", "{\"error\":\"Game not found\"}");
        return;
    }

    if (doc["priority"].is<int>()) {
        int targetPrio = doc["priority"].as<int>();
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
        }
    } else if (strcmp(direction, "up") == 0 && idx > 0) {
        int targetIdx = idx - steps;
        if (targetIdx < 0) targetIdx = 0;
        if (targetIdx != idx) {
            GameEntry moving = g_config.game_queue[idx];
            for (int i = idx; i > targetIdx; i--) {
                g_config.game_queue[i] = g_config.game_queue[i - 1];
            }
            g_config.game_queue[targetIdx] = moving;
        }
    } else if (strcmp(direction, "down") == 0 && idx < g_config.game_queue_count - 1) {
        int targetIdx = idx + steps;
        if (targetIdx >= g_config.game_queue_count) targetIdx = g_config.game_queue_count - 1;
        if (targetIdx != idx) {
            GameEntry moving = g_config.game_queue[idx];
            for (int i = idx; i < targetIdx; i++) {
                g_config.game_queue[i] = g_config.game_queue[i + 1];
            }
            g_config.game_queue[targetIdx] = moving;
        }
    }

    for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
        g_config.game_queue[i].priority = i + 1;
    }

    StorageManager::saveConfig(g_config);
    TwitchAPI::selectNextGameFromQueue();
    server.send(200, "application/json", "{\"status\":\"reordered\"}");
}

void WebServerManager::handleApiGamesClear() {
    g_config.game_queue_count = 0;
    g_config.target_game[0] = '\0';
    g_state.current_channel[0] = '\0';
    g_state.current_channel_id[0] = '\0';
    g_state.current_broadcast_id[0] = '\0';
    StorageManager::saveConfig(g_config);
    Logger::info("Game queue cleared via Web.");
    server.send(200, "application/json", "{\"status\":\"cleared\"}");
}

void WebServerManager::handleApiChannelClear() {
    g_config.target_channel[0] = '\0';
    g_state.current_channel[0] = '\0';
    g_state.current_channel_id[0] = '\0';
    g_state.current_broadcast_id[0] = '\0';
    StorageManager::saveConfig(g_config);
    Logger::info("Streamer channel cleared via Web. Auto-find mode enabled.");
    server.send(200, "application/json", "{\"status\":\"cleared\"}");
}

void WebServerManager::handleNotFound() {
    String uri = server.uri();
    // Silently return 204 for Android/Windows captive portal probes to suppress error logs
    if (uri.indexOf("generate_204") != -1 || uri.indexOf("gen_204") != -1 || uri.indexOf("connecttest") != -1 || uri.indexOf("ncm.txt") != -1) {
        server.send(204, "text/plain", "");
        return;
    }
    // Redirect all other non-matching browser requests to root dashboard
    server.sendHeader("Location", String("http://") + WiFiManager::getIP(), true);
    server.send(302, "text/plain", "");
}
