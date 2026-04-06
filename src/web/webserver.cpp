#include "webserver.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>
#include "../config.h"

extern uint8_t spaceStates[];
extern uint32_t lastPollFinished;
extern int activeWifiSlot;
extern volatile int pollProgress;
extern time_t lastSeenOpen[];
extern bool spacePolling[];

struct SpaceInfo { int led; const char *name; };
static const SpaceInfo spaceInfo[] = {
  {1,"Maakplek"},{2,"HS Drenthe"},{3,"TkkrLab"},{4,"Hack42"},
  {5,"HS Nijmegen"},{6,"TD Venlo"},{7,"ACKspace"},{8,"Hackalot"},
  {9,"Pi4Dec"},{10,"Pixelbar"},{11,"RevSpace"},{12,"Space Leiden"},
  {13,"TechInc"},{14,"AwesomeSpace"},{15,"RandomData"},{16,"HermitHive"},
  {17,"NURDspace"},{18,"Bitlair"}
};

extern bool forcePoll;
extern uint8_t animMode;
extern uint8_t ledBrightness;
extern uint32_t pollIntervalMs;
void saveDisplaySettings();
void startLedTest();
extern String wifiLabel[];
extern String wifiSsid[];
extern String wifiPass[];
void saveWifiSlot(int slot);

WebServer webServer(80);

// Extern MQTT config from main.cpp
extern char mqttBroker[64];
extern uint16_t mqttPort;
extern char mqttTopic[64];
extern bool mqttHAEnable;
extern char mqttUser[64];
extern char mqttPass[64];
extern PubSubClient mqttClient;
void saveMqttSettings();
void loadMqttSettings();

void handleApiMqtt() {
  if (webServer.method() == HTTP_POST) {
    if (webServer.hasArg("broker")) {
      strncpy(mqttBroker, webServer.arg("broker").c_str(), sizeof(mqttBroker)-1);
      mqttBroker[sizeof(mqttBroker)-1] = 0;
    }
    if (webServer.hasArg("port")) {
      mqttPort = webServer.arg("port").toInt();
    }
    if (webServer.hasArg("topic")) {
      strncpy(mqttTopic, webServer.arg("topic").c_str(), sizeof(mqttTopic)-1);
      mqttTopic[sizeof(mqttTopic)-1] = 0;
    }
    if (webServer.hasArg("ha_enable")) {
      mqttHAEnable = (webServer.arg("ha_enable") == "true" || webServer.arg("ha_enable") == "1");
    }
    if (webServer.hasArg("user")) {
      strncpy(mqttUser, webServer.arg("user").c_str(), sizeof(mqttUser)-1);
      mqttUser[sizeof(mqttUser)-1] = 0;
    }
    if (webServer.hasArg("pass")) {
      strncpy(mqttPass, webServer.arg("pass").c_str(), sizeof(mqttPass)-1);
      mqttPass[sizeof(mqttPass)-1] = 0;
    }
    saveMqttSettings();
    // Update MQTT client with new settings immediately
    extern PubSubClient mqttClient;
    mqttClient.setServer(mqttBroker, mqttPort);
    if (mqttClient.connected()) {
      mqttClient.disconnect();
    }
    webServer.send(200, "text/plain", "MQTT settings updated");
    return;
  }
  // GET: always reload settings from NVS to ensure up-to-date values
  loadMqttSettings();
  Serial.printf("[API] MQTT settings: broker='%s' port=%u topic='%s' ha_enable=%d user='%s'\n", mqttBroker, mqttPort, mqttTopic, mqttHAEnable, mqttUser);
  char buf[320];
  snprintf(buf, sizeof(buf),
    "{\"broker\":\"%s\",\"port\":%u,\"topic\":\"%s\",\"ha_enable\":%s,\"user\":\"%s\",\"has_pass\":%s}",
    mqttBroker, mqttPort, mqttTopic, mqttHAEnable ? "true" : "false",
    mqttUser, strlen(mqttPass) > 0 ? "true" : "false");
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "application/json", buf);
}

static const char WEB_ICON_SVG[] PROGMEM = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100">
<defs><clipPath id="c"><rect width="100" height="100" rx="18"/></clipPath></defs>
<g clip-path="url(#c)">
  <rect width="100" height="33" fill="#AE1C28"/>
  <rect y="33" width="100" height="34" fill="#FFFFFF"/>
  <rect y="67" width="100" height="33" fill="#21468B"/>
</g>
<text x="50" y="50" text-anchor="middle" dominant-baseline="central"
  font-family="Arial,sans-serif" font-weight="bold" font-size="42"
  stroke="#21468B" stroke-width="5" paint-order="stroke" fill="white">HS</text>
</svg>)SVG";

static const char WEB_MANIFEST[] PROGMEM = R"JSON({
  "name": "Dutch Hackerspace Map",
  "short_name": "HS Map",
  "description": "Monitor Dutch hackerspaces open/closed status",
  "start_url": "/",
  "display": "standalone",
  "background_color": "#1a1a1a",
  "theme_color": "#21468B",
  "icons": [
    {"src": "/icon.svg", "sizes": "any", "type": "image/svg+xml", "purpose": "any maskable"}
  ]
})JSON";

static const char WEB_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>HackerspaceMap</title>
<link rel="icon" type="image/svg+xml" href="/icon.svg">
<link rel="apple-touch-icon" href="/icon.svg">
<link rel="manifest" href="/manifest.json">
<meta name="theme-color" content="#21468B">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-title" content="HS Map">
<script>(function(){var t=localStorage.getItem('theme')||'dark';document.documentElement.setAttribute('data-theme',t);})();</script>
<style>
:root {
  --bg:#f0f0f0; --container-bg:#fff; --text:#333; --muted:#6c757d;
  --border:#dee2e6; --card-bg:#f8f9fa; --row-border:#dee2e6;
  --topnav-bg:#333; --topnav-text:#f2f2f2; --topnav-hover:#555;
}
[data-theme=dark] {
  --bg:#1a1a1a; --container-bg:#2d2d2d; --text:#fff; --muted:#adb5bd;
  --border:#555; --card-bg:#3a3a3a; --row-border:#444;
  --topnav-bg:#000; --topnav-text:#f2f2f2; --topnav-hover:#444;
}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial,sans-serif;background:var(--bg);color:var(--text);padding-top:60px}
.navbar{position:fixed;top:0;left:0;right:0;height:60px;background:var(--topnav-bg);
  color:var(--topnav-text);display:flex;align-items:center;
  justify-content:space-between;padding:0 20px;z-index:100}
.nav-brand{font-size:1.1em;font-weight:bold;letter-spacing:.05em}
.nav-actions{display:flex;gap:8px;align-items:center}
.theme-btn{background:none;border:none;color:var(--topnav-text);cursor:pointer;
  font-size:1.1em;padding:6px 10px;border-radius:50%;transition:background .2s}
.theme-btn:hover{background:var(--topnav-hover)}
.container{max-width:1000px;margin:0 auto;padding:20px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:15px}
.card{background:var(--card-bg);border:1px solid var(--border);border-radius:6px;padding:20px}
.card h3{font-size:.85em;text-transform:uppercase;letter-spacing:.1em;
  color:#007bff;margin-bottom:14px;border-bottom:1px solid var(--border);padding-bottom:8px}
.row{display:flex;justify-content:space-between;align-items:baseline;
  padding:6px 0;border-bottom:1px solid var(--row-border);gap:10px}
.row:last-child{border-bottom:none}
.lbl{color:var(--muted);font-size:.85em;white-space:nowrap}
.val{font-size:.9em;text-align:right;word-break:break-all}
select,input[type=text],input[type=password]{width:100%;padding:6px 8px;margin-top:4px;
  background:var(--bg);color:var(--text);border:1px solid var(--border);
  border-radius:4px;font-size:.9em;box-sizing:border-box}
.form-row{margin-bottom:10px}
.form-row label{font-size:.82em;color:var(--muted)}
.btn-primary{background:#007bff;color:#fff;border:none;border-radius:4px;
  padding:7px 18px;cursor:pointer;font-size:.9em;margin-top:8px}
.btn-primary:hover{background:#0069d9}
.pass-wrap{position:relative}
.pass-wrap input{padding-right:36px}
.eye-btn{position:absolute;right:8px;top:50%;transform:translateY(-50%);
  background:none;border:none;cursor:pointer;color:var(--muted);font-size:1em}
.presets{display:flex;flex-wrap:wrap;gap:8px;margin-top:4px}
.preset-btn{background:none;border:1px solid var(--border);color:var(--text);
  border-radius:4px;padding:6px 14px;cursor:pointer;font-size:.85em;transition:background .15s,border-color .15s}
.preset-btn:hover{background:var(--border)}
.preset-btn.active{border-color:#007bff;color:#007bff;font-weight:bold;background:rgba(0,123,255,0.15)}
.space-row{display:flex;align-items:center;gap:8px;padding:5px 0;
  border-bottom:1px solid var(--row-border);font-size:.88em}
.space-row:last-child{border-bottom:none}
.space-dot{width:10px;height:10px;border-radius:50%;flex-shrink:0}
.dot-open{background:#28a745}
.dot-closed{background:#dc3545}
.dot-unknown{background:#007bff}
.dot-fetching{background:#ff8c00}
.space-name{flex:1}
.space-badge{font-size:.75em;padding:2px 7px;border-radius:10px;font-weight:bold}
.badge-open{background:#28a74522;color:#28a745}
.badge-closed{background:#dc354522;color:#dc3545}
.badge-unknown{background:#007bff22;color:#007bff}
.poll-time{font-size:.75em;color:var(--muted);margin-top:10px;text-align:right}
.last-open{font-size:.72em;color:var(--muted);display:block;margin-top:1px}
.section-label{font-size:.75em;text-transform:uppercase;letter-spacing:.08em;
  color:var(--muted);margin:14px 0 6px;padding-bottom:4px;border-bottom:1px solid var(--row-border)}
.section-label:first-of-type{margin-top:0}
.anim-option{display:flex;align-items:center;gap:10px;padding:7px 0;
  border-bottom:1px solid var(--row-border);cursor:pointer;font-size:.9em}
.anim-option:last-child{border-bottom:none}
.anim-option input{accent-color:#007bff;width:16px;height:16px;cursor:pointer}
@media(max-width:600px){
  .navbar{padding:0 12px}
  .container{padding:10px}
  .grid{grid-template-columns:1fr}
}
</style>
</head>
<body>
<nav class="navbar">
  <span class="nav-brand">HackerspaceMap</span>
  <div class="nav-actions">
    <button class="theme-btn" id="reboot-btn" onclick="reboot()" title="Reboot" style="color:#dc3545">↺</button>
    <button class="theme-btn" id="theme-btn" onclick="toggleTheme()" title="Toggle dark/light">🌙</button>
  </div>
</nav>
<div class="container">
  <div class="grid">

    <div class="card">
      <h3>Hackerspaces</h3>
      <div id="spaces-summary" style="font-size:.82em;color:var(--muted);margin-bottom:8px;min-height:1.1em"></div>
      <div id="spaces-list"></div>
      <div class="poll-time" id="poll-time">Not yet polled</div>
    </div>
    <div class="card">
      <h3>MQTT</h3>
      <div class="form-row"><label>Broker</label><input type="text" id="mqtt-broker" maxlength="63"></div>
      <div class="form-row"><label>Port</label><input type="text" id="mqtt-port" min="1" max="65535"></div>
      <div class="form-row"><label>Topic</label><input type="text" id="mqtt-topic" maxlength="63"></div>
      <div class="form-row"><label>Username</label><input type="text" id="mqtt-user" maxlength="63" autocomplete="off"></div>
      <div class="form-row"><label>Password</label>
        <div class="pass-wrap">
          <input type="password" id="mqtt-pass" maxlength="63" autocomplete="new-password" placeholder="leave blank to keep current">
          <button class="eye-btn" onclick="toggleMqttPass()" title="Show/hide">👁</button>
        </div>
      </div>
      <div class="form-row"><label><input type="checkbox" id="mqtt-ha-enable"> Publish to Home Assistant</label></div>
      <button class="btn-primary" onclick="saveMqtt()">Update MQTT</button>
    </div>
    <div class="card">
      <h3>WiFi</h3>
      <div class="form-row">
        <label>Slot</label>
        <select id="wifi-slot" onchange="loadWifiSlot()">
          <option value="0">0</option><option value="1">1</option>
          <option value="2">2</option><option value="3">3</option>
          <option value="4">4</option><option value="5">5</option>
        </select>
      </div>
      <div class="form-row"><label>Label</label><input type="text" id="wifi-label" maxlength="20"></div>
      <div class="form-row"><label>SSID</label><input type="text" id="wifi-ssid" maxlength="32"></div>
      <div class="form-row"><label>Password</label>
        <div class="pass-wrap">
          <input type="password" id="wifi-pass" maxlength="64">
          <button class="eye-btn" onclick="togglePass()" title="Show/hide">👁</button>
        </div>
      </div>
      <button class="btn-primary" onclick="saveWifi()">Save slot</button>
    </div>
    <div class="card">
      <h3>Poll interval</h3>
      <button class="btn-primary" onclick="pollNow()" id="poll-now-btn">Update now</button>
      <div class="presets" id="poll-presets" style="margin-top:12px">
        <button class="preset-btn" data-ms="60000">1 min</button>
        <button class="preset-btn" data-ms="120000">2 min</button>
        <button class="preset-btn" data-ms="300000">5 min</button>
        <button class="preset-btn" data-ms="600000">10 min</button>
      </div>
    </div>
    <div class="card">
      <h3>Display</h3>
      <div class="section-label">Brightness</div>
      <div class="presets" id="brightness-presets">
        <button class="preset-btn" data-v="0">Off</button>
        <button class="preset-btn" data-v="3">25%</button>
        <button class="preset-btn" data-v="5">50%</button>
        <button class="preset-btn" data-v="8">75%</button>
        <button class="preset-btn" data-v="10">100%</button>
      </div>
      <div class="section-label">Animation</div>
      <label class="anim-option"><input type="radio" name="anim" value="0"> Sparkle</label>
      <label class="anim-option"><input type="radio" name="anim" value="1"> Breathe</label>
      <label class="anim-option"><input type="radio" name="anim" value="2"> Original</label>
      <div class="section-label">Test</div>
      <button class="btn-primary" id="led-test-btn" onclick="ledTest()">Test LEDs</button>
    </div>
    <div class="card">
      <h3>Hardware</h3>
      <div class="row"><span class="lbl">Chip</span><span class="val" id="hw-chip">…</span></div>
      <div class="row"><span class="lbl">CPU frequency</span><span class="val" id="hw-cpu">…</span></div>
      <div class="row"><span class="lbl">Flash size</span><span class="val" id="hw-flash">…</span></div>
      <div class="row"><span class="lbl">Free heap</span><span class="val" id="hw-heap">…</span></div>
      <div class="row"><span class="lbl">MAC address</span><span class="val" id="hw-mac">…</span></div>
      <div class="row"><span class="lbl">IP address</span><span class="val" id="hw-ip">…</span></div>
      <div class="row"><span class="lbl">WiFi RSSI</span><span class="val" id="hw-rssi">…</span></div>
      <div class="row"><span class="lbl">LEDs</span><span class="val" id="hw-leds">…</span></div>
      <div class="row"><span class="lbl">Uptime</span><span class="val" id="hw-uptime">…</span></div>
      <div class="row"><span class="lbl">WiFi slot</span><span class="val" id="hw-wslot">…</span></div>
      <div class="row"><span class="lbl">Time</span><span class="val" id="hw-time">…</span></div>
      <div class="row"><span class="lbl">Firmware</span><span class="val" id="hw-fw">…</span></div>
    </div>

  </div>
</div>
<footer style="text-align:center;padding:20px;font-size:.8em;color:var(--muted)">
  <a href="https://github.com/hackwinkel/2026DutchHackerspaceMap" target="_blank" rel="noopener" style="color:#007bff;text-decoration:none">Hardware on GitHub</a>
  &nbsp;·&nbsp;
  <a href="https://github.com/javastraat/2026DutchHackerspaceMap" target="_blank" rel="noopener" style="color:#007bff;text-decoration:none">Software on GitHub</a>
</footer>
<script>
const t = localStorage.getItem('theme') || 'dark';
document.getElementById('theme-btn').textContent = t === 'dark' ? '🌙' : '☀️';

function reboot() {
  if (!confirm('Reboot the device?')) return;
  fetch('/api/reboot', {method:'POST'}).catch(()=>{});
  setTimeout(()=>location.reload(), 8000);
}

function toggleTheme() {
  const isDark = document.documentElement.getAttribute('data-theme') === 'dark';
  const next = isDark ? 'light' : 'dark';
  document.documentElement.setAttribute('data-theme', next);
  document.getElementById('theme-btn').textContent = isDark ? '☀️' : '🌙';
  localStorage.setItem('theme', next);
}

function fmtUptime(s) {
  const h = Math.floor(s/3600), m = Math.floor((s%3600)/60), sec = s%60;
  return (h ? h+'h ' : '') + (m ? m+'m ' : '') + sec+'s';
}

function pollHw() {
  fetch('/api/hw').then(r=>r.json()).then(d=>{
    document.getElementById('hw-chip').textContent  = d.chip;
    document.getElementById('hw-cpu').textContent   = d.cpu_mhz+' MHz';
    document.getElementById('hw-flash').textContent = (d.flash_kb/1024).toFixed(1)+' MB';
    document.getElementById('hw-heap').textContent  = d.free_heap_kb.toFixed(1)+' KB free';
    document.getElementById('hw-mac').textContent   = d.mac;
    document.getElementById('hw-ip').textContent    = d.ip;
    document.getElementById('hw-rssi').textContent  = d.rssi+' dBm';
    document.getElementById('hw-leds').textContent  = d.map_leds+' map + '+d.backlight_leds+' backlight';
    document.getElementById('hw-uptime').textContent = fmtUptime(d.uptime_s);
    document.getElementById('hw-wslot').textContent  = d.wifi_slot >= 0 ? 'Slot '+d.wifi_slot : 'SoftAP';
    document.getElementById('hw-time').textContent   = d.time_str || '—';
    document.getElementById('hw-fw').textContent     = d.fw_build;
  }).catch(()=>{});
}

pollHw();
setInterval(pollHw, 5000);

function pollAnim() {
  fetch('/api/anim').then(r=>r.json()).then(d=>{
    document.querySelectorAll('input[name=anim]').forEach(r=>r.checked=(parseInt(r.value)===d.mode));
  }).catch(()=>{});
}
document.querySelectorAll('input[name=anim]').forEach(r=>{
  r.addEventListener('change', ()=>{
    fetch('/api/anim?mode='+r.value, {method:'POST'}).catch(()=>{});
  });
});
pollAnim();

function pollBrightness() {
  fetch('/api/brightness').then(r=>r.json()).then(d=>{
    document.querySelectorAll('#brightness-presets .preset-btn').forEach(b=>{
      b.classList.toggle('active', parseInt(b.dataset.v)===d.brightness);
    });
  }).catch(()=>{});
}
// --- MQTT config ---
function loadMqtt() {
  fetch('/api/mqtt').then(r=>r.json()).then(d=>{
    document.getElementById('mqtt-broker').value = d.broker;
    document.getElementById('mqtt-port').value = d.port;
    document.getElementById('mqtt-topic').value = d.topic;
    document.getElementById('mqtt-user').value = d.user || '';
    document.getElementById('mqtt-pass').placeholder = d.has_pass ? 'leave blank to keep current' : 'no password set';
    document.getElementById('mqtt-ha-enable').checked = d.ha_enable === true || d.ha_enable === 'true';
  }).catch(()=>{});
}
function saveMqtt() {
  const broker = document.getElementById('mqtt-broker').value;
  const port = document.getElementById('mqtt-port').value;
  const topic = document.getElementById('mqtt-topic').value;
  const user = document.getElementById('mqtt-user').value;
  const pass = document.getElementById('mqtt-pass').value;
  const ha_enable = document.getElementById('mqtt-ha-enable').checked ? 'true' : 'false';
  let body = `broker=${encodeURIComponent(broker)}&port=${encodeURIComponent(port)}&topic=${encodeURIComponent(topic)}&ha_enable=${ha_enable}&user=${encodeURIComponent(user)}`;
  if (pass !== '') body += `&pass=${encodeURIComponent(pass)}`;
  fetch('/api/mqtt', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body})
    .then(()=>{ alert('MQTT settings updated!'); document.getElementById('mqtt-pass').value=''; loadMqtt(); }).catch(()=>{});
}
function toggleMqttPass() {
  const p = document.getElementById('mqtt-pass');
  p.type = p.type === 'password' ? 'text' : 'password';
}
loadMqtt();
document.querySelectorAll('#brightness-presets .preset-btn').forEach(b=>{
  b.addEventListener('click', ()=>{
    document.querySelectorAll('#brightness-presets .preset-btn').forEach(x=>x.classList.remove('active'));
    b.classList.add('active');
    fetch('/api/brightness?v='+b.dataset.v, {method:'POST'}).catch(()=>{});
  });
});
pollBrightness();

function loadWifiSlot() {
  const slot = document.getElementById('wifi-slot').value;
  fetch('/api/wifi-slot?slot='+slot).then(r=>r.json()).then(d=>{
    document.getElementById('wifi-slot').querySelector('option[value="'+slot+'"]').textContent = slot+' – '+d.label;
    document.getElementById('wifi-label').value = d.label;
    document.getElementById('wifi-ssid').value  = d.ssid;
    document.getElementById('wifi-pass').value  = d.password;
  }).catch(()=>{});
}
function saveWifi() {
  const slot  = document.getElementById('wifi-slot').value;
  const label = document.getElementById('wifi-label').value;
  const ssid  = document.getElementById('wifi-ssid').value;
  const pass  = document.getElementById('wifi-pass').value;
  const body  = 'slot='+slot+'&label='+encodeURIComponent(label)+'&ssid='+encodeURIComponent(ssid)+'&password='+encodeURIComponent(pass);
  fetch('/api/save-wifi-slot', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body})
    .then(()=>{ loadAllSlotLabels(); alert('Slot '+slot+' saved!'); }).catch(()=>{});
}
function togglePass() {
  const p = document.getElementById('wifi-pass');
  p.type = p.type === 'password' ? 'text' : 'password';
}
let spacesTimer = null;
function scheduleSpacesPoll(ms) {
  clearTimeout(spacesTimer);
  spacesTimer = setTimeout(pollSpaces, ms);
}
function pollSpaces() {
  fetch('/api/spaces').then(r=>r.json()).then(d=>{
    const polling = d.poll_progress !== null && d.poll_progress !== undefined;
    const list = document.getElementById('spaces-list');
    list.innerHTML = d.spaces.map(s=>{
      const cls = s.fetching ? 'fetching' : s.state==='open' ? 'open' : s.state==='closed' ? 'closed' : 'unknown';
      const badge = s.fetching ? `<span class="space-badge" style="background:#ff8c0022;color:#ff8c00">fetching…</span>`
                               : `<span class="space-badge badge-${cls}">${s.state}</span>`;
      const lastOpen = s.last_open && s.state!=='open' && !s.fetching
        ? `<span class="last-open">last open ${s.last_open}</span>` : '';
      return `<div class="space-row">
        <div class="space-dot dot-${cls}"></div>
        <span class="space-name">${s.name}${lastOpen}</span>
        ${badge}
      </div>`;
    }).join('');
    const open = d.spaces.filter(s=>s.state==='open').length;
    const closed = d.spaces.filter(s=>s.state==='closed').length;
    const unknown = d.spaces.length - open - closed;
    document.getElementById('spaces-summary').textContent =
      polling ? 'Polling '+d.poll_progress+'/'+d.spaces.length+'…'
              : open+' open · '+closed+' closed · '+unknown+' unknown';
    if (polling) {
      document.getElementById('poll-time').textContent = 'Polling in progress…';
    } else if (d.polled_ago_s !== null) {
      const ago = d.polled_ago_s < 60 ? d.polled_ago_s+'s ago'
                : Math.floor(d.polled_ago_s/60)+'m ago';
      const timeStr = d.last_poll_str ? d.last_poll_str+' ('+ago+')' : ago;
      document.getElementById('poll-time').textContent = 'Last polled: '+timeStr;
    }
    scheduleSpacesPoll(polling ? 2000 : 15000);
  }).catch(()=>{ scheduleSpacesPoll(15000); });
}
pollSpaces();

function loadAllSlotLabels() {
  for (let i=0; i<6; i++) {
    fetch('/api/wifi-slot?slot='+i).then(r=>r.json()).then(d=>{
      const opt = document.getElementById('wifi-slot').querySelector('option[value="'+i+'"]');
      opt.textContent = i+' – '+d.label;
    }).catch(()=>{});
  }
}
loadAllSlotLabels();
loadWifiSlot();

function pollNow() {
  const btn = document.getElementById('poll-now-btn');
  btn.disabled = true;
  btn.textContent = 'Polling…';
  fetch('/api/poll-now', {method:'POST'}).then(()=>{
    setTimeout(()=>{ btn.disabled=false; btn.textContent='Update now'; pollSpaces(); }, 5000);
  }).catch(()=>{ btn.disabled=false; btn.textContent='Update now'; });
}

function pollPoll() {
  fetch('/api/poll').then(r=>r.json()).then(d=>{
    document.querySelectorAll('.preset-btn').forEach(b=>{
      b.classList.toggle('active', parseInt(b.dataset.ms)===d.interval_ms);
    });
  }).catch(()=>{});
}
document.querySelectorAll('#poll-presets .preset-btn').forEach(b=>{
  b.addEventListener('click', ()=>{
    document.querySelectorAll('#poll-presets .preset-btn').forEach(x=>x.classList.remove('active'));
    b.classList.add('active');
    fetch('/api/poll?ms='+b.dataset.ms, {method:'POST'}).catch(()=>{});
  });
});
pollPoll();

function ledTest() {
  const btn = document.getElementById('led-test-btn');
  btn.disabled = true;
  btn.textContent = 'Testing…';
  fetch('/api/led-test', {method:'POST'}).catch(()=>{});
  setTimeout(()=>{ btn.disabled=false; btn.textContent='Test LEDs'; }, 5000);
}
</script>
</body>
</html>)HTML";

void sendJson(const String &json) {
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "application/json", json);
}

void handleRoot() {
  webServer.send_P(200, "text/html", WEB_HTML);
}

void handleIcon() {
  webServer.send_P(200, "image/svg+xml", WEB_ICON_SVG);
}

void handleManifest() {
  webServer.send_P(200, "application/manifest+json", WEB_MANIFEST);
}

void handleApiHw() {
  char timeBuf[10] = "—";
  time_t now = time(nullptr);
  if (now > 1000000000UL) { // NTP synced (year > 2001)
    struct tm *tm_info = localtime(&now);
    strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", tm_info);
  }
  char buf[768];
  snprintf(buf, sizeof(buf),
    "{"
      "\"chip\":\"ESP32-C3\","
      "\"cpu_mhz\":%u,"
      "\"flash_kb\":%u,"
      "\"free_heap_kb\":%.1f,"
      "\"mac\":\"%s\","
      "\"ip\":\"%s\","
      "\"rssi\":%d,"
      "\"map_leds\":%d,"
      "\"backlight_leds\":%d,"
      "\"uptime_s\":%u,"
      "\"wifi_slot\":%d,"
      "\"time_str\":\"%s\","
      "\"fw_build\":\"%s %s\""
    "}",
    getCpuFrequencyMhz(),
    (unsigned)(ESP.getFlashChipSize() / 1024),
    ESP.getFreeHeap() / 1024.0f,
    WiFi.macAddress().c_str(),
    WiFi.localIP().toString().c_str(),
    WiFi.RSSI(),
    MAP_LED_COUNT,
    BACKLIGHT_COUNT,
    (uint32_t)(millis() / 1000),
    activeWifiSlot,
    timeBuf,
    __DATE__, __TIME__
  );
  sendJson(buf);
}

void handleApiSpaces() {
  String json = "{\"spaces\":[";
  time_t now = time(nullptr);
  for (int i = 0; i < 18; i++) {
    const char *state = spaceStates[i] == 1 ? "open"
                      : spaceStates[i] == 0 ? "closed"
                      : "unknown";
    if (i > 0) json += ',';
    json += "{\"led\":";  json += spaceInfo[i].led;
    json += ",\"name\":\""; json += spaceInfo[i].name;
    json += "\",\"state\":\""; json += state;
    json += "\",\"fetching\":"; json += spacePolling[i] ? "true" : "false";
    json += ",\"last_open\":";
    time_t lo = lastSeenOpen[i];
    if (lo > 0 && now > 1000000000UL) {
      char buf[24];
      uint32_t ago = (uint32_t)(now - lo);
      if      (ago < 60)    snprintf(buf, sizeof(buf), "\"just now\"");
      else if (ago < 3600)  snprintf(buf, sizeof(buf), "\"%um ago\"", ago / 60);
      else if (ago < 86400) {
        struct tm *t = localtime(&lo);
        char tb[6]; strftime(tb, sizeof(tb), "%H:%M", t);
        snprintf(buf, sizeof(buf), "\"at %s\"", tb);
      } else                snprintf(buf, sizeof(buf), "\"%ud ago\"", ago / 86400);
      json += buf;
    } else {
      json += "null";
    }
    json += "}";
  }
  json += "],\"poll_progress\":";
  json += pollProgress >= 0 ? String(pollProgress) : "null";
  json += ",\"polled_ago_s\":";
  json += lastPollFinished > 0 ? String((millis() - lastPollFinished) / 1000) : "null";
  if (lastPollFinished > 0 && now > 1000000000UL) {
    uint32_t ago_s = (millis() - lastPollFinished) / 1000;
    time_t poll_ts = now - (time_t)ago_s;
    struct tm *tm_info = localtime(&poll_ts);
    char tbuf[10];
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", tm_info);
    json += ",\"last_poll_str\":\"";
    json += tbuf;
    json += "\"";
  }
  json += "}";
  sendJson(json);
}

void handleApiGetWifiSlot() {
  int slot = webServer.hasArg("slot") ? webServer.arg("slot").toInt() : 0;
  if (slot < 0 || slot >= WIFI_SLOT_COUNT) slot = 0;
  char buf[256];
  snprintf(buf, sizeof(buf),
    "{\"label\":\"%s\",\"ssid\":\"%s\",\"password\":\"%s\"}",
    wifiLabel[slot].c_str(), wifiSsid[slot].c_str(), wifiPass[slot].c_str());
  sendJson(buf);
}

void handleApiSaveWifiSlot() {
  if (!webServer.hasArg("slot") || !webServer.hasArg("ssid")) {
    webServer.send(400, "text/plain", "Missing args");
    return;
  }
  int slot = webServer.arg("slot").toInt();
  if (slot < 0 || slot >= WIFI_SLOT_COUNT) { webServer.send(400, "text/plain", "Bad slot"); return; }
  wifiLabel[slot] = webServer.arg("label");
  wifiSsid[slot]  = webServer.arg("ssid");
  wifiPass[slot]  = webServer.arg("password");
  saveWifiSlot(slot);
  webServer.send(200, "text/plain", "Slot saved");
}

void handleApiPollNow() {
  forcePoll = true;
  webServer.send(200, "text/plain", "ok");
}

void handleApiPoll() {
  if (webServer.method() == HTTP_POST && webServer.hasArg("ms")) {
    uint32_t ms = webServer.arg("ms").toInt();
    if (ms >= 60000 && ms <= 3600000) {
      pollIntervalMs = ms;
      saveDisplaySettings();
      Serial.printf("Poll interval -> %u ms\n", pollIntervalMs);
    }
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "{\"interval_ms\":%u}", pollIntervalMs);
  sendJson(buf);
}

void handleApiReboot() {
  webServer.send(200, "text/plain", "Rebooting...");
  delay(500);
  ESP.restart();
}

void handleApiLedTest() {
  startLedTest();
  webServer.send(200, "text/plain", "ok");
}

void handleApiBrightness() {
  if (webServer.method() == HTTP_POST && webServer.hasArg("v")) {
    int v = webServer.arg("v").toInt();
    if (v >= 0 && v <= 10) {
      ledBrightness = v;
      saveDisplaySettings();
      Serial.printf("Brightness -> %d\n", ledBrightness);
    }
  }
  char buf[24];
  snprintf(buf, sizeof(buf), "{\"brightness\":%d}", ledBrightness);
  sendJson(buf);
}

void handleApiAnim() {
  if (webServer.method() == HTTP_POST) {
    if (webServer.hasArg("mode")) {
      uint8_t m = webServer.arg("mode").toInt();
      if (m <= ANIM_MODE_ORIGINAL) {
        animMode = m;
        saveDisplaySettings();
        Serial.printf("Animation mode -> %d\n", animMode);
      }
    }
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "{\"mode\":%d}", animMode);
  sendJson(buf);
}

void setupWebServer() {
  webServer.on("/",                  HTTP_GET,  handleRoot);
  webServer.on("/icon.svg",          HTTP_GET,  handleIcon);
  webServer.on("/manifest.json",     HTTP_GET,  handleManifest);
  webServer.on("/api/spaces",        HTTP_GET,  handleApiSpaces);
  webServer.on("/api/hw",            HTTP_GET,  handleApiHw);
  webServer.on("/api/wifi-slot",     HTTP_GET,  handleApiGetWifiSlot);
  webServer.on("/api/save-wifi-slot",HTTP_POST, handleApiSaveWifiSlot);
  webServer.on("/api/reboot",        HTTP_POST, handleApiReboot);
  webServer.on("/api/led-test",      HTTP_POST, handleApiLedTest);
  webServer.on("/api/brightness",    HTTP_ANY,  handleApiBrightness);
  webServer.on("/api/poll-now",      HTTP_POST, handleApiPollNow);
  webServer.on("/api/poll",          HTTP_ANY,  handleApiPoll);
  webServer.on("/api/anim",          HTTP_ANY,  handleApiAnim);
  webServer.on("/api/mqtt",          HTTP_ANY,  handleApiMqtt);
  webServer.begin();
  Serial.println("Web server started on port 80");
}
