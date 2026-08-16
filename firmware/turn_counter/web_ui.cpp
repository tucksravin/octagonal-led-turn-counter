#include "web_ui.h"
#include <WebServer.h>
#include <WiFi.h>
#include <octagon_core.h>

static WebServer server(80);
static TableConfig cfg = {nullptr, 0};
static TableHooks hooks = {nullptr, nullptr, nullptr, nullptr};
static bool started = false;

// String::toInt() returns 0 for non-numeric input, which would quietly turn
// "?value=abc" into a successful request for mode 0. Validate the digits first.
static bool parseUInt(const String &s, long &out) {
  if (s.length() == 0 || s.length() > 3) return false;
  for (unsigned int i = 0; i < s.length(); i++) {
    if (!isDigit(s[i])) return false;
  }
  out = s.toInt();
  return true;
}

// One self-contained page: no external stylesheets, fonts or scripts. The board
// has no internet path, so anything external simply would not load.
static const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Turn Counter</title>
<style>
:root{color-scheme:dark}
*{box-sizing:border-box}
body{margin:0;padding:20px;background:#111;color:#eee;
     font:16px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
h1{font-size:19px;margin:0 0 18px;letter-spacing:.02em}
.card{background:#1c1c1e;border-radius:14px;padding:16px;margin-bottom:14px}
.lbl{font-size:13px;text-transform:uppercase;letter-spacing:.08em;color:#8e8e93;margin-bottom:10px}
button{font:inherit;color:inherit;background:#2c2c2e;border:2px solid transparent;
       border-radius:11px;padding:14px;width:100%;text-align:left;margin-bottom:8px}
button:last-child{margin-bottom:0}
button[aria-pressed="true"]{border-color:#0a84ff;background:#0a84ff22}
#power{text-align:center;font-weight:600}
#power[data-lit="false"]{background:#3a3a3c;color:#8e8e93}
input[type=range]{width:100%;height:34px}
.row{display:flex;align-items:center;gap:10px}
.dots{display:flex;gap:7px;margin-top:12px}
.dot{width:22px;height:22px;border-radius:50%;border:2px solid #3a3a3c;
     display:flex;align-items:center;justify-content:center;font-size:11px;color:#000}
.swatch{width:15px;height:15px;border-radius:4px;flex:none}
#banner{background:#ff9f0a22;border:1px solid #ff9f0a;color:#ff9f0a;
        border-radius:11px;padding:11px;margin-bottom:14px;font-size:14px}
.hide{display:none}
#err{color:#ff453a}
.note{font-size:12px;color:#8e8e93;margin-top:9px}
details{margin-top:4px}
summary{font-size:13px;text-transform:uppercase;letter-spacing:.08em;color:#8e8e93;
        padding:6px 0;cursor:pointer}
table{width:100%;border-collapse:collapse;font-size:13px;margin-top:8px}
th,td{text-align:left;padding:5px 4px;border-bottom:1px solid #2c2c2e}
th{color:#8e8e93;font-weight:500}
td.warn{color:#ff9f0a}
#diagmeta{font-size:12px;color:#8e8e93;margin-top:10px}
</style></head><body>
<h1>Turn Counter</h1>
<div id="banner" class="hide">Setup in progress at the table &mdash; mode changes are locked.</div>
<div class="card"><button id="power" data-lit="true">Turn off</button></div>
<div class="card">
  <div class="lbl">Now</div>
  <div class="row"><div class="swatch" id="swatch"></div><div id="now">&hellip;</div></div>
  <div class="dots" id="dots"></div>
</div>
<div class="card"><div class="lbl">Mode</div><div id="modes"></div></div>
<div class="card">
  <div class="lbl">Brightness <span id="bripct"></span></div>
  <input type="range" id="bri" min="5" max="100" step="5">
  <div class="note">All-on scenes are already at the power cap, so above ~60% only
  single-seat play gets brighter.</div>
</div>
<div class="card"><details id="diagbox">
  <summary>Diagnostics</summary>
  <table><thead><tr><th>Seat</th><th>Pin</th><th>Baseline</th><th>LEDs</th><th>Last tap</th></tr></thead>
  <tbody id="diagrows"></tbody></table>
  <div id="diagmeta"></div>
  <button id="diagrefresh" style="margin-top:12px;text-align:center">Refresh</button>
</details></div>
<div id="err"></div>
<script>
let cfg=null, lastLocal=0, pending=null, dragging=false;
const $=i=>document.getElementById(i);

async function post(path){
  try{
    const r=await fetch(path,{method:'POST'});
    if(r.status===409){ $('err').textContent='Table is in setup mode - try again in a moment.'; return; }
    if(!r.ok){ $('err').textContent='Rejected: '+await r.text(); return; }
    $('err').textContent=''; render(await r.json());
  }catch(e){ $('err').textContent='Table not responding.'; }
}

function render(s){
  $('power').textContent = s.lit ? 'Turn off' : 'Turn on';
  $('power').dataset.lit = s.lit;
  $('banner').classList.toggle('hide', !s.setup);

  document.querySelectorAll('#modes button').forEach((b,i)=>
    b.setAttribute('aria-pressed', i===s.mode));

  if(s.currentSide<0){
    $('now').textContent = s.lit ? cfg.modes[s.mode] : 'Off';
    $('swatch').style.background='transparent';
  }else{
    $('now').textContent = 'Seat '+s.currentSide+' - '+cfg.modes[s.mode]+(s.lit?'':' (off)');
    $('swatch').style.background = cfg.colors[s.currentSide];
  }

  $('dots').innerHTML='';
  for(let i=0;i<cfg.sides;i++){
    const d=document.createElement('div'); d.className='dot'; d.textContent=i;
    const inRoster = (s.roster>>i)&1;
    if(inRoster) d.style.background = ((s.ready>>i)&1) ? '#30d158' : cfg.colors[i];
    else d.style.opacity=.25;
    $('dots').appendChild(d);
  }

  // Don't fight the finger: ignore polled brightness right after a local change.
  if(!dragging && Date.now()-lastLocal>2000){ $('bri').value=s.brightness; }
  $('bripct').textContent=$('bri').value+'%';
}

function ago(ms){
  if(ms<0) return 'never';
  if(ms<1000) return 'just now';
  if(ms<60000) return Math.round(ms/1000)+'s ago';
  return Math.round(ms/60000)+'m ago';
}

async function loadDiag(){
  try{
    const d = await (await fetch('/api/diag')).json();
    // Flag a baseline well above the pack. A piezo losing its ground return
    // reads high and steady, which is invisible unless you know the norm.
    const sorted=d.sides.map(s=>s.baseline).sort((a,b)=>a-b);
    const median=sorted[Math.floor(sorted.length/2)]||1;
    $('diagrows').innerHTML = d.sides.map((s,i)=>
      '<tr><td>'+i+'</td><td>'+s.pin+'</td><td'+
      (s.baseline > median*4 ? ' class="warn"' : '')+'>'+s.baseline+
      '</td><td>'+s.leds+'</td><td>'+ago(s.sinceTapMs)+'</td></tr>').join('');
    $('diagmeta').textContent =
      'tap fires at baseline + '+d.tapDelta+
      ' · up '+Math.round(d.uptimeMs/60000)+'m'+
      ' · heap '+Math.round(d.freeHeap/1024)+'k'+
      ' · wifi '+d.rssi+' dBm';
  }catch(e){ $('diagmeta').textContent='Diagnostics unavailable.'; }
}

async function poll(){
  try{
    const r=await fetch('/api/state');
    if(r.ok){ $('err').textContent=''; render(await r.json()); }
  }catch(e){ $('err').textContent='Table not responding.'; }
}

(async ()=>{
  cfg = await (await fetch('/api/config')).json();
  cfg.modes.forEach((name,i)=>{
    const b=document.createElement('button');
    b.textContent=name;
    b.onclick=()=>post('/api/mode?value='+i);
    $('modes').appendChild(b);
  });
  $('power').onclick=()=>post('/api/power?value='+
    ($('power').dataset.lit==='true'?'off':'on'));

  const bri=$('bri');
  const send=()=>{ lastLocal=Date.now(); post('/api/brightness?value='+bri.value); };
  bri.oninput=()=>{
    $('bripct').textContent=bri.value+'%';
    dragging=true; lastLocal=Date.now();
    if(pending) return;                       // throttle to 1 POST / 250 ms
    pending=setTimeout(()=>{ pending=null; send(); },250);
  };
  bri.onchange=()=>{ dragging=false; send(); };

  // preventDefault: the button lives inside <details>, and without it a click
  // collapses the block you're trying to refresh.
  $('diagbox').ontoggle=()=>{ if($('diagbox').open) loadDiag(); };
  $('diagrefresh').onclick=(e)=>{ e.preventDefault(); loadDiag(); };

  await poll();
  setInterval(poll,2000);
})();
</script></body></html>)HTML";

// Hand-rolled JSON: six fields don't justify a library's flash cost. Fixed
// buffers, no heap churn in a handler that runs from the game loop.
static void sendState(int code) {
  TableState s;
  hooks.read(s);
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"mode\":%u,\"brightness\":%u,\"lit\":%s,\"currentSide\":%d,"
           "\"roster\":%u,\"ready\":%u,\"setup\":%s}",
           s.mode, s.brightnessPercent, s.lit ? "true" : "false", s.currentSide,
           s.rosterMask, s.readyMask, s.inSetupMode ? "true" : "false");
  server.send(code, "application/json", buf);
}

static void handleState() { sendState(200); }

// Static facts, fetched once by the page. Split from /api/state so the 2 s poll
// stays small, and so mode names and seat colours are single-sourced from the
// firmware instead of duplicated into the page.
static void handleConfig() {
  char buf[512];
  int n = snprintf(buf, sizeof(buf), "{\"sides\":%u,\"colors\":[", NUM_SIDES);
  for (uint8_t i = 0; i < NUM_SIDES && n < (int)sizeof(buf); i++) {
    n += snprintf(buf + n, sizeof(buf) - n, "%s\"#%02X%02X%02X\"", i ? "," : "",
                  PLAYER_COLORS[i].r, PLAYER_COLORS[i].g, PLAYER_COLORS[i].b);
  }
  if (n < (int)sizeof(buf)) n += snprintf(buf + n, sizeof(buf) - n, "],\"modes\":[");
  for (uint8_t i = 0; i < cfg.modeCount && n < (int)sizeof(buf); i++) {
    n += snprintf(buf + n, sizeof(buf) - n, "%s\"%s\"", i ? "," : "", cfg.modeNames[i]);
  }
  if (n < (int)sizeof(buf)) snprintf(buf + n, sizeof(buf) - n, "]}");
  server.send(200, "application/json", buf);
}

// Read-only bench diagnostics, so a piezo can be judged from a phone instead of
// a laptop and a reboot — the baselines are otherwise printed once at boot and
// never again. Deliberately not part of the 2 s poll: this is something you go
// and look at.
static void handleDiag() {
  char buf[768];
  int n = snprintf(buf, sizeof(buf),
                   "{\"tapDelta\":%u,\"uptimeMs\":%lu,\"freeHeap\":%lu,\"rssi\":%d,\"sides\":[",
                   TAP_DELTA, (unsigned long)millis(),
                   (unsigned long)ESP.getFreeHeap(), WiFi.RSSI());
  uint32_t now = millis();
  for (uint8_t i = 0; i < NUM_SIDES && n > 0 && n < (int)sizeof(buf); i++) {
    uint32_t last = lastTapForSide(i);
    // 0 means "never fired". Reporting now - 0 would read as "tapped at boot",
    // the opposite of the truth, in exactly the case you're diagnosing.
    long since = (last == 0) ? -1L : (long)(now - last);
    n += snprintf(buf + n, sizeof(buf) - n,
                  "%s{\"pin\":%u,\"baseline\":%u,\"leds\":%u,\"sinceTapMs\":%ld}",
                  i ? "," : "", sidePiezoPin[i], baseline(i), sideLedCounts[i], since);
  }
  if (n > 0 && n < (int)sizeof(buf)) snprintf(buf + n, sizeof(buf) - n, "]}");
  server.send(200, "application/json", buf);
}

// Range checking lives here, not in the setters, because this side knows the
// bounds — and it's what lets a rejection be 400 (impossible) rather than 409
// (possible, but not right now).

static void handleMode() {
  long v;
  if (!server.hasArg("value") || !parseUInt(server.arg("value"), v)) {
    server.send(400, "text/plain", "value must be a number");
    return;
  }
  if (v >= cfg.modeCount) {
    server.send(400, "text/plain", "mode out of range");
    return;
  }
  if (!hooks.setMode((uint8_t)v)) {
    server.send(409, "text/plain", "setup in progress at the table");
    return;
  }
  sendState(200);
}

static void handleBrightness() {
  long v;
  if (!server.hasArg("value") || !parseUInt(server.arg("value"), v)) {
    server.send(400, "text/plain", "value must be a number");
    return;
  }
  if (v < BRIGHTNESS_MIN_PCT || v > 100) {
    server.send(400, "text/plain", "brightness must be 5-100");
    return;
  }
  if (!hooks.setBrightness((uint8_t)v)) {
    server.send(409, "text/plain", "busy");
    return;
  }
  sendState(200);
}

static void handlePower() {
  String v = server.arg("value");
  if (v != "on" && v != "off") {
    server.send(400, "text/plain", "value must be on or off");
    return;
  }
  hooks.setPower(v == "on");
  sendState(200);
}

static void handleRoot() {
  server.send_P(200, "text/html", PAGE_HTML);
}

void webUiBegin(const TableConfig &c, const TableHooks &h) {
  if (started) return;
  cfg = c;
  hooks = h;

  server.on("/",               HTTP_GET,  handleRoot);
  server.on("/api/config",     HTTP_GET,  handleConfig);
  server.on("/api/state",      HTTP_GET,  handleState);
  server.on("/api/diag",       HTTP_GET,  handleDiag);
  server.on("/api/mode",       HTTP_POST, handleMode);
  server.on("/api/brightness", HTTP_POST, handleBrightness);
  server.on("/api/power",      HTTP_POST, handlePower);
  server.onNotFound([]() { server.send(404, "text/plain", "no such endpoint"); });

  server.begin();
  started = true;
  Serial.println("Web UI ready on port 80");
}

void webUiHandle() { if (started) server.handleClient(); }

void webUiEnd() {
  if (!started) return;
  server.stop();
  started = false;
}
