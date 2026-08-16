#include "web_server.h"
#include "logger.h"
#include "storage.h"
#include "wifi_mgr.h"
#include "twitch_api.h"
#include "led_indicator.h"
#include <ArduinoJson.h>
#include <Update.h>

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
            --card-bg: rgba(30, 41, 59, 0.75);
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
        .container { max-width: 1050px; margin: 0 auto; }
        header {
            display: flex; justify-content: space-between; align-items: center;
            padding: 20px; background: var(--card-bg); backdrop-filter: blur(12px);
            border: 1px solid var(--border-color); border-radius: 16px; margin-bottom: 24px;
        }
        .logo { display: flex; align-items: center; gap: 12px; }
        .logo svg { fill: var(--primary); width: 32px; height: 32px; filter: drop-shadow(0 0 8px var(--primary-glow)); }
        .logo h1 { font-size: 1.25rem; font-weight: 700; background: linear-gradient(135deg, #fff, #94a3b8); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }
        .header-badges { display: flex; gap: 10px; align-items: center; }
        .status-badge {
            padding: 6px 14px; border-radius: 20px; font-size: 0.85rem; font-weight: 600;
            background: rgba(16, 185, 129, 0.15); color: var(--success); border: 1px solid rgba(16, 185, 129, 0.3);
        }
        .account-badge {
            padding: 6px 14px; border-radius: 20px; font-size: 0.85rem; font-weight: 600;
            background: rgba(145, 70, 255, 0.15); color: #c084fc; border: 1px solid rgba(145, 70, 255, 0.3);
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
        .queue-list { display: flex; flex-direction: column; gap: 10px; }
        .queue-item {
            display: flex; flex-direction: column; gap: 8px;
            padding: 12px 16px; border-radius: 12px;
            background: rgba(15, 23, 42, 0.5); border: 1px solid var(--border-color);
            transition: border-color 0.3s, background 0.3s;
        }
        .queue-item:hover { border-color: var(--primary); background: rgba(15, 23, 42, 0.8); }
        .queue-item.auto-discovered { border-style: dashed; border-color: rgba(245, 158, 11, 0.3); }
        .queue-item.completed { opacity: 0.5; }
        .queue-top { display: flex; align-items: center; gap: 10px; }
        .queue-num {
            min-width: 28px; height: 28px; border-radius: 50%;
            display: flex; align-items: center; justify-content: center;
            font-size: 0.75rem; font-weight: 700;
            background: var(--primary); color: #fff;
        }
        .queue-item.completed .queue-num { background: var(--success); }
        .queue-item.auto-discovered .queue-num { background: var(--warning); }
        .queue-info { flex: 1; min-width: 0; }
        .queue-name { font-weight: 600; font-size: 0.95rem; }
        .queue-meta { display: flex; gap: 8px; align-items: center; margin-top: 3px; }
        .queue-status {
            font-size: 0.7rem; font-weight: 600; padding: 2px 8px; border-radius: 10px;
            text-transform: uppercase; letter-spacing: 0.5px;
        }
        .status-farming { background: rgba(145, 70, 255, 0.2); color: var(--primary); }
        .status-queued { background: rgba(6, 182, 212, 0.2); color: var(--accent); }
        .status-completed { background: rgba(16, 185, 129, 0.2); color: var(--success); }
        .status-auto { background: rgba(245, 158, 11, 0.2); color: var(--warning); }
        .queue-actions { display: flex; gap: 4px; }
        .streamer-chips { display: flex; flex-wrap: wrap; gap: 6px; align-items: center; margin-top: 4px; }
        .streamer-chip {
            display: inline-flex; align-items: center; gap: 6px;
            background: rgba(145, 70, 255, 0.15); border: 1px solid rgba(145, 70, 255, 0.3);
            color: #d8b4fe; padding: 3px 8px; border-radius: 14px; font-size: 0.75rem;
        }
        .streamer-chip .chip-del { cursor: pointer; font-weight: bold; color: var(--danger); }
        .streamer-add-row { display: flex; gap: 6px; margin-top: 4px; }
        .streamer-add-row input { padding: 4px 8px; font-size: 0.75rem; }

        /* Multi-Account Cards */
        .acc-list { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 10px; margin-bottom: 12px; }
        .acc-card {
            padding: 12px; border-radius: 10px; background: rgba(15, 23, 42, 0.5);
            border: 1px solid var(--border-color); display: flex; flex-direction: column; gap: 6px;
        }
        .acc-card.active { border-color: var(--primary); background: rgba(145, 70, 255, 0.1); }
        .acc-header { display: flex; justify-content: space-between; align-items: center; font-weight: 600; }
        .acc-sub { font-size: 0.75rem; color: var(--text-muted); }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <div class="logo">
                <svg viewBox="0 0 24 24"><path d="M11.571 4.714h1.715v5.143H11.57zm4.715 0H18v5.143h-1.714zM6 0L1.714 4.286v15.428h5.143V24l4.286-4.286h3.428L22.286 12V0zm14.571 11.143l-3.428 3.428h-3.429l-3 3v-3H6.857V1.714h13.714Z"/></svg>
                <div>
                    <h1>ESP32-S3 Twitch Drops Farmer</h1>
                    <div style="font-size: 0.75rem; color: var(--text-muted);">v1.2.0 &bull; ESP32-S3 (N16R8)</div>
                </div>
            </div>
            <div class="header-badges">
                <div id="accBadge" class="account-badge">Account: Main</div>
                <div id="statusBadge" class="status-badge">Connecting...</div>
            </div>
        </header>

        <!-- Stats Grid -->
        <div class="grid">
            <div class="card">
                <h2><span>Active Game & Streamer</span> <span style="font-size: 0.75rem; color: var(--primary);">Live Tracking</span></h2>
                <div class="stat-value" id="curGame">--</div>
                <div class="stat-sub" id="curStreamer">Streamer: Searching...</div>
                <div class="stat-sub" style="color: var(--text-muted); margin-top: 4px;" id="jitterInfo">Heartbeat: 60s</div>
            </div>

            <div class="card">
                <h2><span>Drop Campaign Progress</span> <span id="progressPctText">0%</span></h2>
                <div class="stat-value" id="dropName">No active drop</div>
                <div class="progress-bar-bg">
                    <div class="progress-bar-fill" id="progressBar"></div>
                </div>
            </div>

            <div class="card">
                <h2><span>Rewards & Statistics</span> <span style="font-size: 0.75rem; color: var(--success);">Farm Total</span></h2>
                <div class="stat-value" id="totalFarmed">0 min</div>
                <div class="stat-sub" id="rewardsStats">Drops: 0 claimed &bull; Points: 0</div>
            </div>
        </div>

        <!-- Multi-Account Section -->
        <div class="card card-full" style="margin-bottom: 24px;">
            <h2><span>Twitch Accounts (Multi-Account Switcher)</span> <button class="btn btn-sm btn-secondary" onclick="toggleAccAdd()">+ Add Account</button></h2>
            <div class="acc-list" id="accListContainer"></div>
            <div id="accAddForm" style="display: none; padding-top: 10px; border-top: 1px solid var(--border-color);">
                <div style="display: flex; gap: 10px;">
                    <input type="text" id="newAccName" placeholder="Profile Name (e.g. Alt_1)" style="flex: 1;">
                    <input type="password" id="newAccToken" placeholder="OAuth Token" style="flex: 2;">
                    <button class="btn btn-sm" onclick="submitAddAccount()">Save Account</button>
                </div>
            </div>
        </div>

        <!-- Priority Game Queue Section -->
        <div class="card card-full" style="margin-bottom: 24px;">
            <h2>
                <span>Priority Game Queue</span>
                <span style="display: flex; gap: 6px;">
                    <button class="btn btn-sm btn-secondary" onclick="clearQueue()">Clear Queue</button>
                    <button class="btn btn-sm btn-secondary" onclick="fetchGames()">Refresh</button>
                </span>
            </h2>
            <div style="display: flex; gap: 10px; margin-bottom: 16px;">
                <input type="text" id="newGameName" placeholder="Game Name (e.g. Rust, GTA V, Brawlhalla)" style="flex: 1;">
                <button class="btn btn-sm" onclick="addGame()">+ Add Game</button>
            </div>
            <div class="queue-list" id="queueContainer">
                <div style="color: var(--text-muted); text-align: center; padding: 20px;">Loading queue...</div>
            </div>
        </div>

        <!-- Settings and OTA Grid -->
        <div class="grid">
            <div class="card">
                <h2>Configuration & Settings</h2>
                <form id="cfgForm" onsubmit="saveConfig(event)">
                    <div class="form-group">
                        <label>Wi-Fi SSID</label>
                        <input type="text" id="wifi_ssid" name="wifi_ssid">
                    </div>
                    <div class="form-group">
                        <label>Wi-Fi Password</label>
                        <input type="password" id="wifi_pass" name="wifi_pass">
                    </div>
                    <div class="form-group">
                        <label>Active OAuth Token</label>
                        <input type="password" id="oauth_token" name="oauth_token">
                    </div>
                    <div class="form-group">
                        <label>Global Target Streamer Override (optional)</label>
                        <input type="text" id="target_channel" name="target_channel" placeholder="Leave empty for auto-find">
                    </div>
                    <div class="form-group" style="display: flex; gap: 20px; align-items: center; margin-top: 10px;">
                        <label style="margin: 0; display: flex; align-items: center; gap: 6px; cursor: pointer;">
                            <input type="checkbox" id="auto_claim" name="auto_claim"> Auto-Claim Drops
                        </label>
                        <label style="margin: 0; display: flex; align-items: center; gap: 6px; cursor: pointer;">
                            <input type="checkbox" id="auto_pts" name="auto_pts"> Auto Channel Points
                        </label>
                        <label style="margin: 0; display: flex; align-items: center; gap: 6px; cursor: pointer;">
                            <input type="checkbox" id="led_on" name="led_on"> RGB LED
                        </label>
                    </div>
                    <button type="submit" class="btn btn-full" style="margin-top: 12px;">Save Settings</button>
                </form>
            </div>

            <div class="card">
                <h2><span>Web OTA Firmware Update</span> <span style="font-size: 0.75rem; color: var(--accent);">Wireless Flash</span></h2>
                <p style="font-size: 0.8rem; color: var(--text-muted); margin-bottom: 14px;">Flash updated firmware (.bin) directly over Wi-Fi without USB cable.</p>
                <form method="POST" action="/update" enctype="multipart/form-data" id="otaForm" onsubmit="uploadOta(event)">
                    <div class="form-group">
                        <label>Select firmware.bin file</label>
                        <input type="file" id="otaFile" name="update" accept=".bin" style="width: 100%; color: #fff; font-size: 0.85rem;">
                    </div>
                    <div class="progress-bar-bg" id="otaBarBg" style="display: none; margin-bottom: 12px;">
                        <div class="progress-bar-fill" id="otaBar" style="width: 0%;"></div>
                    </div>
                    <button type="submit" class="btn btn-full btn-secondary" id="otaBtn">Upload & Flash Firmware</button>
                </form>
                <div style="margin-top: 16px; display: flex; gap: 10px;">
                    <button class="btn btn-sm btn-secondary btn-full" onclick="forceClaim()">Force Claim Drops</button>
                    <button class="btn btn-sm btn-danger btn-full" onclick="restartBoard()">Reboot ESP32</button>
                </div>
            </div>
        </div>

        <!-- Live Serial Logs -->
        <div class="card card-full">
            <h2>Live System Logs</h2>
            <div class="log-box" id="logBox"></div>
        </div>
    </div>

    <script>
        async function fetchStatus() {
            try {
                const res = await fetch('/api/status');
                const data = await res.json();
                
                document.getElementById('statusBadge').innerText = data.farming_active ? 'Farming Active' : 'Idle';
                document.getElementById('statusBadge').style.color = data.farming_active ? 'var(--success)' : 'var(--warning)';
                
                document.getElementById('accBadge').innerText = 'Account: ' + (data.active_account || 'Main');
                document.getElementById('curGame').innerText = data.current_game || '<auto-search>';
                document.getElementById('curStreamer').innerText = 'Streamer: ' + (data.current_channel || '<searching>');
                document.getElementById('jitterInfo').innerText = 'Heartbeat: ' + Math.round((data.heartbeat_interval_ms || 60000) / 1000) + 's (Jitter active)';
                
                document.getElementById('dropName').innerText = data.active_drop_name || 'No active campaign';
                document.getElementById('progressPctText').innerText = data.drop_progress_pct + '%';
                document.getElementById('progressBar').style.width = data.drop_progress_pct + '%';
                
                document.getElementById('totalFarmed').innerText = data.total_minutes_watched + ' min';
                document.getElementById('rewardsStats').innerText = 'Drops: ' + data.drops_claimed_count + ' claimed \u2022 Points: ' + (data.points_claimed_count || 0);
            } catch (e) {
                console.error(e);
            }
        }

        async function fetchAccounts() {
            try {
                const res = await fetch('/api/accounts');
                const list = await res.json();
                const container = document.getElementById('accListContainer');
                container.innerHTML = '';
                list.forEach(a => {
                    const div = document.createElement('div');
                    div.className = 'acc-card ' + (a.active ? 'active' : '');
                    div.innerHTML = `
                        <div class="acc-header">
                            <span>${a.name}</span>
                            ${a.active ? '<span class="status-badge" style="font-size: 0.65rem; padding: 2px 6px;">Active</span>' : `<button class="btn btn-sm btn-secondary" onclick="switchAccount(${a.id})">Switch</button>`}
                        </div>
                        <div class="acc-sub">User: ${a.user_login || '<not fetched>'}</div>
                        <div class="acc-sub">Farmed: ${a.total_minutes}m &bull; Drops: ${a.drops_claimed} &bull; Pts: ${a.points_claimed || 0}</div>
                    `;
                    container.appendChild(div);
                });
            } catch (e) {
                console.error(e);
            }
        }

        async function switchAccount(id) {
            await fetch('/api/accounts/switch', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ id: id })
            });
            fetchAccounts();
            fetchStatus();
        }

        function toggleAccAdd() {
            const form = document.getElementById('accAddForm');
            form.style.display = (form.style.display === 'none') ? 'block' : 'none';
        }

        async function submitAddAccount() {
            const name = document.getElementById('newAccName').value.trim();
            const token = document.getElementById('newAccToken').value.trim();
            if (!name || !token) return;
            await fetch('/api/accounts/add', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ name: name, token: token })
            });
            document.getElementById('newAccName').value = '';
            document.getElementById('newAccToken').value = '';
            toggleAccAdd();
            fetchAccounts();
        }

        async function fetchGames() {
            try {
                const res = await fetch('/api/games');
                const games = await res.json();
                const container = document.getElementById('queueContainer');
                container.innerHTML = '';
                if (!games || games.length === 0) {
                    container.innerHTML = '<div style="color: var(--text-muted); text-align: center; padding: 20px;">Queue is empty. Auto-discovery will find drop campaigns automatically.</div>';
                    return;
                }
                games.forEach((g, idx) => {
                    const div = document.createElement('div');
                    div.className = 'queue-item ' + (g.status === 'COMPLETED' ? 'completed ' : '') + (g.status === 'AUTO' ? 'auto-discovered ' : '');
                    
                    let streamersHtml = '';
                    if (g.streamers && g.streamers.length > 0) {
                        g.streamers.forEach(st => {
                            streamersHtml += `<span class="streamer-chip"><span>@${st}</span><span class="chip-del" onclick="removeStreamer('${g.name}', '${st}')">&times;</span></span>`;
                        });
                    }

                    div.innerHTML = `
                        <div class="queue-top">
                            <div class="queue-num">${idx + 1}</div>
                            <div class="queue-info">
                                <div class="queue-name">${g.name}</div>
                                <div class="queue-meta">
                                    <span class="queue-status status-${g.status.toLowerCase()}">${g.status}</span>
                                    <span style="font-size: 0.75rem; color: var(--text-muted);">${g.progress_pct}% (${g.minutes_watched} min)</span>
                                </div>
                            </div>
                            <div class="queue-actions">
                                <button class="btn btn-sm btn-secondary" onclick="reorderGame('${g.name}', 'up')">&uarr;</button>
                                <button class="btn btn-sm btn-secondary" onclick="reorderGame('${g.name}', 'down')">&darr;</button>
                                <button class="btn btn-sm btn-danger" onclick="removeGame('${g.name}')">&times;</button>
                            </div>
                        </div>
                        <div style="font-size: 0.75rem; color: var(--text-muted); margin-top: 4px;">Preferred Streamers (${g.streamers ? g.streamers.length : 0}/3):</div>
                        <div class="streamer-chips">
                            ${streamersHtml || '<span style="font-size: 0.75rem; color: var(--text-muted); font-style: italic;">Auto-search top live category streamer</span>'}
                        </div>
                        ${(!g.streamers || g.streamers.length < 3) ? `
                            <div class="streamer-add-row">
                                <input type="text" id="stInput_${idx}" placeholder="Streamer login (e.g. shiko)" style="flex: 1;">
                                <button class="btn btn-sm btn-secondary" onclick="addStreamer('${g.name}', document.getElementById('stInput_${idx}').value)">+ Add</button>
                            </div>
                        ` : ''}
                    `;
                    container.appendChild(div);
                });
            } catch (e) {
                console.error(e);
            }
        }

        async function addStreamer(game, streamer) {
            if (!streamer || !streamer.trim()) return;
            await fetch('/api/streamers/add', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ game: game, streamer: streamer.trim() })
            });
            fetchGames();
        }

        async function removeStreamer(game, streamer) {
            await fetch('/api/streamers/remove', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ game: game, streamer: streamer })
            });
            fetchGames();
        }

        async function addGame() {
            const name = document.getElementById('newGameName').value.trim();
            if (!name) return;
            await fetch('/api/games/add', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ name: name })
            });
            document.getElementById('newGameName').value = '';
            fetchGames();
        }

        async function removeGame(name) {
            await fetch('/api/games/remove', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ name: name })
            });
            fetchGames();
        }

        async function reorderGame(name, direction) {
            await fetch('/api/games/reorder', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ name: name, direction: direction })
            });
            fetchGames();
        }

        async function clearQueue() {
            if (!confirm('Clear all games from priority queue?')) return;
            await fetch('/api/games/clear', { method: 'POST' });
            fetchGames();
        }

        async function fetchLogs() {
            try {
                const res = await fetch('/api/logs');
                const logs = await res.json();
                const box = document.getElementById('logBox');
                box.innerHTML = logs.map(l => `<div class="log-line log-${l.level}">[${l.time}] [${l.level}] ${l.msg}</div>`).join('');
                box.scrollTop = box.scrollHeight;
            } catch (e) {
                console.error(e);
            }
        }

        async function fetchConfig() {
            try {
                const res = await fetch('/api/config');
                const cfg = await res.json();
                document.getElementById('wifi_ssid').value = cfg.wifi_ssid || '';
                document.getElementById('oauth_token').value = cfg.oauth_token || '';
                document.getElementById('target_channel').value = cfg.target_channel || '';
                document.getElementById('auto_claim').checked = cfg.auto_claim !== false;
                document.getElementById('auto_pts').checked = cfg.auto_pts !== false;
                document.getElementById('led_on').checked = cfg.led_on !== false;
            } catch (e) {
                console.error(e);
            }
        }

        async function saveConfig(e) {
            e.preventDefault();
            const payload = {
                wifi_ssid: document.getElementById('wifi_ssid').value,
                wifi_pass: document.getElementById('wifi_pass').value,
                oauth_token: document.getElementById('oauth_token').value,
                target_channel: document.getElementById('target_channel').value,
                auto_claim: document.getElementById('auto_claim').checked,
                auto_pts: document.getElementById('auto_pts').checked,
                led_on: document.getElementById('led_on').checked
            };
            await fetch('/api/config', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(payload)
            });
            alert('Settings saved!');
            fetchConfig();
        }

        async function uploadOta(e) {
            e.preventDefault();
            const file = document.getElementById('otaFile').files[0];
            if (!file) {
                alert('Please select a firmware .bin file first!');
                return;
            }
            const barBg = document.getElementById('otaBarBg');
            const bar = document.getElementById('otaBar');
            const btn = document.getElementById('otaBtn');
            barBg.style.display = 'block';
            btn.disabled = true;
            btn.innerText = 'Uploading Firmware...';

            const xhr = new XMLHttpRequest();
            xhr.open('POST', '/update');
            xhr.upload.onprogress = function(evt) {
                if (evt.lengthComputable) {
                    const pct = Math.round((evt.loaded / evt.total) * 100);
                    bar.style.width = pct + '%';
                }
            };
            xhr.onload = function() {
                if (xhr.status === 200) {
                    bar.style.width = '100%';
                    btn.innerText = 'Update Complete! Rebooting...';
                    setTimeout(() => { window.location.reload(); }, 8000);
                } else {
                    alert('OTA Update Failed: ' + xhr.responseText);
                    btn.disabled = false;
                    btn.innerText = 'Upload & Flash Firmware';
                }
            };
            const formData = new FormData();
            formData.append('update', file);
            xhr.send(formData);
        }

        async function forceClaim() {
            await fetch('/api/claim', { method: 'POST' });
            alert('Drop check triggered!');
        }

        async function restartBoard() {
            if (confirm('Reboot ESP32-S3?')) {
                await fetch('/api/restart', { method: 'POST' });
            }
        }

        // Init loops
        fetchStatus();
        fetchAccounts();
        fetchGames();
        fetchLogs();
        fetchConfig();
        setInterval(fetchStatus, 3000);
        setInterval(fetchLogs, 2500);
    </script>
</body>
</html>
)rawliteral";

void WebServerManager::init() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/status", HTTP_GET, handleApiStatus);
    server.on("/api/config", HTTP_GET, handleApiConfig);
    server.on("/api/config", HTTP_POST, handleApiConfig);
    server.on("/api/logs", HTTP_GET, handleApiLogs);
    server.on("/api/wifi/scan", HTTP_GET, handleApiWifiScan);
    server.on("/api/restart", HTTP_POST, handleApiRestart);
    server.on("/api/claim", HTTP_POST, []() {
        TwitchAPI::fetchInventoryAndProgress();
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // Games Queue API
    server.on("/api/games", HTTP_GET, handleApiGames);
    server.on("/api/games/add", HTTP_POST, handleApiGamesAdd);
    server.on("/api/games/remove", HTTP_POST, handleApiGamesRemove);
    server.on("/api/games/reorder", HTTP_POST, handleApiGamesReorder);
    server.on("/api/games/clear", HTTP_POST, handleApiGamesClear);

    // Preferred Streamers API
    server.on("/api/streamers/add", HTTP_POST, handleApiStreamerAdd);
    server.on("/api/streamers/remove", HTTP_POST, handleApiStreamerRemove);

    // Multi-Account API
    server.on("/api/accounts", HTTP_GET, handleApiAccounts);
    server.on("/api/accounts/switch", HTTP_POST, handleApiAccountsSwitch);
    server.on("/api/accounts/add", HTTP_POST, handleApiAccountsAdd);
    server.on("/api/accounts/remove", HTTP_POST, handleApiAccountsRemove);
    server.on("/api/accounts/rotate", HTTP_POST, handleApiAccountsRotate);

    server.on("/api/channel/clear", HTTP_POST, handleApiChannelClear);

    // Web OTA Firmware Update Endpoint
    server.on("/update", HTTP_POST, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", (Update.hasError()) ? "UPDATE FAILED" : "OK");
        delay(500);
        ESP.restart();
    }, []() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            Logger::info("Web OTA Start: %s", upload.filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Logger::error("OTA Begin Failed");
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Logger::error("OTA Write Failed");
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) {
                Logger::info("Web OTA Success: %u bytes flashed!", upload.totalSize);
            } else {
                Logger::error("OTA End Failed");
                Update.printError(Serial);
            }
        }
    });

    server.onNotFound(handleNotFound);
    server.begin();
    Logger::info("HTTP Web Server initialized on port 80");
}

void WebServerManager::handleClient() {
    server.handleClient();
}

void WebServerManager::handleRoot() {
    server.send(200, "text/html", INDEX_HTML);
}

void WebServerManager::handleApiStatus() {
    JsonDocument doc;
    doc["wifi_connected"] = g_state.wifi_connected;
    doc["farming_active"] = g_config.farming_enabled && g_state.wifi_connected && strlen(g_state.user_login) > 0;
    doc["pubsub_connected"] = g_state.pubsub_connected;
    doc["current_ip"] = g_state.current_ip;
    doc["user_login"] = g_state.user_login;
    doc["active_account"] = (g_config.active_account_idx < g_config.account_count) ? g_config.accounts[g_config.active_account_idx].name : "Main";
    doc["current_game"] = (g_config.target_game[0] != '\0') ? g_config.target_game : g_state.current_game;
    doc["current_channel"] = g_state.current_channel;
    doc["active_drop_name"] = g_state.active_drop_name;
    doc["drop_progress_pct"] = g_state.drop_progress_pct;
    doc["total_minutes_watched"] = g_state.total_minutes_watched;
    doc["drops_claimed_count"] = g_state.drops_claimed_count;
    doc["points_claimed_count"] = g_state.channel_points_claimed_count;
    doc["heartbeat_interval_ms"] = g_state.current_heartbeat_interval_ms ? g_state.current_heartbeat_interval_ms : 60000;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["free_psram"] = ESP.getFreePsram();

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void WebServerManager::handleApiAccounts() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (uint8_t i = 0; i < g_config.account_count; i++) {
        const AccountProfile& a = g_config.accounts[i];
        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = i;
        obj["name"] = a.name;
        obj["user_login"] = a.user_login;
        obj["user_id"] = a.user_id;
        obj["total_minutes"] = a.total_minutes;
        obj["drops_claimed"] = a.drops_claimed;
        obj["points_claimed"] = a.points_claimed;
        obj["active"] = (i == g_config.active_account_idx);
        obj["enabled"] = a.enabled;
    }

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void WebServerManager::handleApiAccountsSwitch() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    uint8_t id = doc["id"] | 0;
    if (TwitchAPI::switchAccount(id)) {
        server.send(200, "application/json", "{\"status\":\"switched\"}");
    } else {
        server.send(400, "application/json", "{\"error\":\"Switch failed\"}");
    }
}

void WebServerManager::handleApiAccountsAdd() {
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
    const char* token = doc["token"] | "";
    if (strlen(name) == 0 || strlen(token) == 0) {
        server.send(400, "application/json", "{\"error\":\"Name and token required\"}");
        return;
    }
    if (g_config.account_count >= MAX_ACCOUNTS) {
        server.send(400, "application/json", "{\"error\":\"Max accounts reached\"}");
        return;
    }
    AccountProfile& a = g_config.accounts[g_config.account_count];
    strncpy(a.name, name, sizeof(a.name) - 1);
    String cleanToken = token;
    if (cleanToken.startsWith("oauth:")) cleanToken = cleanToken.substring(6);
    strncpy(a.oauth_token, cleanToken.c_str(), sizeof(a.oauth_token) - 1);
    a.enabled = true;
    a.total_minutes = 0;
    a.drops_claimed = 0;
    a.points_claimed = 0;
    g_config.account_count++;
    StorageManager::saveConfig(g_config);
    server.send(200, "application/json", "{\"status\":\"added\"}");
}

void WebServerManager::handleApiAccountsRemove() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    uint8_t id = doc["id"] | 0;
    if (id < g_config.account_count) {
        for (uint8_t i = id; i < g_config.account_count - 1; i++) {
            g_config.accounts[i] = g_config.accounts[i + 1];
        }
        g_config.account_count--;
        if (g_config.active_account_idx >= g_config.account_count && g_config.account_count > 0) {
            g_config.active_account_idx = 0;
        }
        StorageManager::saveConfig(g_config);
        server.send(200, "application/json", "{\"status\":\"removed\"}");
    } else {
        server.send(404, "application/json", "{\"error\":\"Not found\"}");
    }
}

void WebServerManager::handleApiAccountsRotate() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    g_config.account_rotation_enabled = doc["enabled"] | false;
    StorageManager::saveConfig(g_config);
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void WebServerManager::handleApiStreamerAdd() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    const char* game = doc["game"] | "";
    const char* streamer = doc["streamer"] | "";
    if (strlen(game) == 0 || strlen(streamer) == 0) {
        server.send(400, "application/json", "{\"error\":\"Game and streamer required\"}");
        return;
    }
    for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
        if (strcasestr(g_config.game_queue[i].name, game) != NULL) {
            GameEntry& entry = g_config.game_queue[i];
            if (entry.streamer_count >= MAX_PREFERRED_STREAMERS) {
                server.send(400, "application/json", "{\"error\":\"Max 3 streamers reached\"}");
                return;
            }
            strncpy(entry.preferred_streamers[entry.streamer_count], streamer, sizeof(entry.preferred_streamers[0]) - 1);
            entry.streamer_count++;
            StorageManager::saveConfig(g_config);
            server.send(200, "application/json", "{\"status\":\"added\"}");
            return;
        }
    }
    server.send(404, "application/json", "{\"error\":\"Game not found\"}");
}

void WebServerManager::handleApiStreamerRemove() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    const char* game = doc["game"] | "";
    const char* streamer = doc["streamer"] | "";
    for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
        if (strcasestr(g_config.game_queue[i].name, game) != NULL) {
            GameEntry& entry = g_config.game_queue[i];
            int sIdx = -1;
            for (uint8_t s = 0; s < entry.streamer_count; s++) {
                if (strcasecmp(entry.preferred_streamers[s], streamer) == 0) {
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
                server.send(200, "application/json", "{\"status\":\"removed\"}");
                return;
            }
        }
    }
    server.send(404, "application/json", "{\"error\":\"Streamer not found\"}");
}

void WebServerManager::handleApiConfig() {
    if (server.method() == HTTP_GET) {
        JsonDocument doc;
        doc["wifi_ssid"] = g_config.wifi_ssid;
        doc["oauth_token"] = g_config.oauth_token;
        doc["client_id"] = g_config.client_id;
        doc["target_channel"] = g_config.target_channel;
        doc["auto_claim"] = g_config.auto_claim;
        doc["auto_pts"] = g_config.auto_claim_points;
        doc["led_on"] = g_config.led_enabled;
        doc["check_interval_sec"] = g_config.check_interval_sec;
        doc["farming_enabled"] = g_config.farming_enabled;

        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    } else if (server.method() == HTTP_POST) {
        if (!server.hasArg("plain")) {
            server.send(400, "application/json", "{\"error\":\"Missing body\"}");
            return;
        }

        JsonDocument doc;
        if (deserializeJson(doc, server.arg("plain"))) {
            server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        if (doc["wifi_ssid"].is<const char*>()) strncpy(g_config.wifi_ssid, doc["wifi_ssid"], sizeof(g_config.wifi_ssid));
        if (doc["wifi_pass"].is<const char*>() && strlen(doc["wifi_pass"]) > 0) strncpy(g_config.wifi_pass, doc["wifi_pass"], sizeof(g_config.wifi_pass));
        if (doc["oauth_token"].is<const char*>()) {
            String token = doc["oauth_token"].as<String>();
            token.trim();
            if (token.startsWith("oauth:")) token = token.substring(6);
            strncpy(g_config.oauth_token, token.c_str(), sizeof(g_config.oauth_token));
            if (g_config.active_account_idx < g_config.account_count) {
                strncpy(g_config.accounts[g_config.active_account_idx].oauth_token, token.c_str(), sizeof(g_config.accounts[0].oauth_token));
            }
        }
        if (doc["target_channel"].is<const char*>()) strncpy(g_config.target_channel, doc["target_channel"], sizeof(g_config.target_channel));
        if (doc["auto_claim"].is<bool>()) g_config.auto_claim = doc["auto_claim"];
        if (doc["auto_pts"].is<bool>()) g_config.auto_claim_points = doc["auto_pts"];
        if (doc["led_on"].is<bool>()) {
            g_config.led_enabled = doc["led_on"];
            LedIndicator::setEnabled(g_config.led_enabled);
        }

        StorageManager::saveConfig(g_config);
        Logger::info("Configuration updated via Web Dashboard.");
        server.send(200, "application/json", "{\"status\":\"saved\"}");
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

void WebServerManager::handleApiGames() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
        const GameEntry& e = g_config.game_queue[i];
        JsonObject obj = arr.add<JsonObject>();
        obj["name"] = e.name;
        obj["priority"] = e.priority;
        obj["progress_pct"] = e.progress_pct;
        obj["minutes_watched"] = e.minutes_watched;
        switch (e.status) {
            case GAME_FARMING: obj["status"] = "FARMING"; break;
            case GAME_COMPLETED: obj["status"] = "COMPLETED"; break;
            case GAME_AUTO_DISCOVERED: obj["status"] = "AUTO"; break;
            default: obj["status"] = "QUEUED"; break;
        }
        JsonArray stArr = obj["streamers"].to<JsonArray>();
        for (uint8_t s = 0; s < e.streamer_count; s++) {
            stArr.add(e.preferred_streamers[s]);
        }
    }

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
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
    for (uint8_t i = 0; i < g_config.game_queue_count; i++) {
        if (strcasestr(g_config.game_queue[i].name, name) != NULL) {
            server.send(400, "application/json", "{\"error\":\"Game already in queue\"}");
            return;
        }
    }
    GameEntry& entry = g_config.game_queue[g_config.game_queue_count];
    strncpy(entry.name, name, sizeof(entry.name) - 1);
    entry.name[sizeof(entry.name) - 1] = '\0';
    entry.priority = g_config.game_queue_count + 1;
    entry.status = GAME_QUEUED;
    entry.progress_pct = 0;
    entry.minutes_watched = 0;
    entry.streamer_count = 0;
    g_config.game_queue_count++;
    StorageManager::saveConfig(g_config);
    TwitchAPI::selectNextGameFromQueue();
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
    if (strcmp(direction, "up") == 0 && idx > 0) {
        GameEntry temp = g_config.game_queue[idx];
        g_config.game_queue[idx] = g_config.game_queue[idx - 1];
        g_config.game_queue[idx - 1] = temp;
    } else if (strcmp(direction, "down") == 0 && idx < g_config.game_queue_count - 1) {
        GameEntry temp = g_config.game_queue[idx];
        g_config.game_queue[idx] = g_config.game_queue[idx + 1];
        g_config.game_queue[idx + 1] = temp;
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
    server.send(200, "application/json", "{\"status\":\"cleared\"}");
}

void WebServerManager::handleApiChannelClear() {
    g_config.target_channel[0] = '\0';
    g_state.current_channel[0] = '\0';
    g_state.current_channel_id[0] = '\0';
    g_state.current_broadcast_id[0] = '\0';
    StorageManager::saveConfig(g_config);
    server.send(200, "application/json", "{\"status\":\"cleared\"}");
}

void WebServerManager::handleNotFound() {
    String uri = server.uri();
    if (uri.indexOf("generate_204") != -1 || uri.indexOf("gen_204") != -1 || uri.indexOf("connecttest") != -1 || uri.indexOf("ncm.txt") != -1) {
        server.send(204, "text/plain", "");
        return;
    }
    server.sendHeader("Location", String("http://") + WiFiManager::getIP(), true);
    server.send(302, "text/plain", "");
}
