#include <octagon_core.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include "web_ui.h"

// The full game: a roster of seats, four interaction modes, and a tap-gesture
// setup mode to configure both. The table hardware underneath — strip geometry,
// the calibrated side table, piezo tap detection — lives in the octagon_core
// library, shared with the `eight` sketch.
//
// WiFi/OTA is what pushes this past the stock 1.25 MB app partition, so it
// builds with min_spiffs (see the Makefile).

// Wi-Fi/OTA credentials live in a gitignored secrets.h — copy secrets.example.h
// and fill it in. Absent that file the SSID is empty and setupWiFi() skips the
// radio entirely, so a fresh clone still compiles and runs.
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #define WIFI_SSID     ""
  #define WIFI_PASSWORD ""
  #define OTA_HOSTNAME  "turn-counter"
  #define OTA_PASSWORD  ""
#endif

const uint32_t WIFI_CONNECT_TIMEOUT_MS = 5000;
const uint32_t WIFI_RETRY_MS           = 30000;  // how often loop() re-attempts a down link

const uint8_t  SETUP_TAP_COUNT          = 4;
const uint16_t SETUP_TAP_WINDOW_MS      = 2000;
const uint16_t MODE_DEMO_MS             = 5000;   // phase 1: each mode's whole-table demo runs ~5 s
const uint16_t MODE_ABORT_IDLE_MS       = 25000;  // phase 1: no tap for a full demo rotation — abort, change nothing
const uint16_t MODE_TAP_GRACE_MS        = 500;    // swallow gesture spillover right after entry
const uint16_t SETUP_JOIN_IDLE_MS       = 5000;   // phase 2: idle commits the roster

// Mode-demo animation timing (phase 1).
const uint16_t DEMO_SNAKE_MS            = 15;     // CW/CCW: snake advances one LED (~3.3 s per lap)
const uint8_t  DEMO_SNAKE_LEN           = 14;     // CW/CCW: snake length in LEDs, head bright, tail fading
const uint16_t DEMO_HOP_MS              = 600;    // ARB: solid dwell per seat; a round covers all 8
const uint16_t DEMO_FLIP_MS             = 250;    // READY: one more side flipping to green
const uint16_t DEMO_HOLD_MS             = 500;    // READY: all-green pause before snapping back

// READY mode plays in red/green, not player colors: red = not ready, green = in.
const CRGB READY_RED   = CRGB(200, 0, 0);
const CRGB READY_GREEN = CRGB(0, 200, 0);

// Interaction modes chosen on the setup dial. CW/CCW/ARB are turn-passing
// variants; READY is a group ready-check with no single "current" seat.
enum GameMode : uint8_t { MODE_CW = 0, MODE_CCW = 1, MODE_ARB = 2, MODE_READY = 3, MODE_COUNT = 4 };

// Setup runs in two phases: the whole table demos candidate modes until any tap
// commits the one showing, then seats tap themselves into the roster.
enum SetupPhase : uint8_t { PHASE_MODE = 0, PHASE_JOIN = 1 };

Preferences prefs;      // "turntable": game state

bool     sideActive[NUM_SIDES] = {true, true, true, true, true, true, true, true};  // roster: which seats are "in"
int8_t   currentSide = 0;        // the active seat whose turn it is (index into sideActive)
uint8_t  prevRosterMask = 0xFF;  // roster snapshot taken on setup entry; restored on abort or a 0-seat exit
bool     inSetupMode = false;
bool     otaActive = false;
bool     netServicesUp = false;     // OTA listener + web UI are bound to the current link
uint32_t lastWifiAttemptMs = 0;
bool     tableLit = true;           // false = LEDs dark, game state preserved. Never persisted:
                                    // plug in = on, so a power cut can't leave it looking broken.

uint8_t  gameMode = MODE_CW;            // persisted interaction mode
int8_t   joinOrder[NUM_SIDES] = {0};    // seats in the order they joined (ARB turn order)
uint8_t  joinCount = 0;
SetupPhase setupPhase = PHASE_MODE;
uint8_t  dialMode = MODE_CW;            // mode currently being demoed in phase 1
uint32_t setupEnteredMs = 0;            // when setup opened (anchors the tap grace window)
uint32_t demoModeStartMs = 0;           // when the current demo began (auto-advance + animation clock)
int8_t   prevJoinOrder[NUM_SIDES] = {0};// join-order snapshot paired with prevRosterMask
uint8_t  prevJoinCount = 0;
bool     ready[NUM_SIDES] = {false};    // READY mode: green (ready) vs red, active seats only

uint32_t firstTapInBurstMs = 0;
uint8_t  tapsInBurst = 0;
int8_t   burstSide = -1;   // the side the current tap-burst is accumulating on

// Demo colors for the mode-select phase; READY demos in red/green instead.
const CRGB MODE_COLORS[MODE_COUNT] = {
  CRGB(60, 220, 80),    // CW    - green
  CRGB(40, 120, 255),   // CCW   - blue
  CRGB(220, 60, 220),   // ARB   - magenta
  CRGB(255, 130, 30)    // READY - orange
};
const char* const MODE_NAMES[MODE_COUNT] = {
  "clockwise", "counter-clockwise", "arbitrary (join order)", "ready-or-not"
};

uint8_t rosterMask() {
  uint8_t m = 0;
  for (uint8_t s = 0; s < NUM_SIDES; s++) if (sideActive[s]) m |= (1 << s);
  return m;
}

void applyRosterMask(uint8_t m) {
  for (uint8_t s = 0; s < NUM_SIDES; s++) sideActive[s] = (m >> s) & 1;
}

uint8_t activeCount() {
  uint8_t n = 0;
  for (uint8_t s = 0; s < NUM_SIDES; s++) if (sideActive[s]) n++;
  return n;
}

int8_t firstActiveSide() {
  for (uint8_t s = 0; s < NUM_SIDES; s++) if (sideActive[s]) return s;
  return 0;
}

// Next active seat clockwise from `from`, skipping empty seats and wrapping.
// Returns `from` unchanged if it's the only active seat.
int8_t nextActiveSide(int8_t from) {
  for (uint8_t k = 1; k <= NUM_SIDES; k++) {
    uint8_t s = (from + k) % NUM_SIDES;
    if (sideActive[s]) return s;
  }
  return from;
}

// Next active seat counter-clockwise from `from`, skipping empties and wrapping.
int8_t prevActiveSide(int8_t from) {
  for (uint8_t k = 1; k <= NUM_SIDES; k++) {
    int8_t s = (from - k + NUM_SIDES) % NUM_SIDES;
    if (sideActive[s]) return s;
  }
  return from;
}

// Next seat in join order (ARB mode), wrapping. Returns `from` if it's the only one.
int8_t nextInJoinOrder(int8_t from) {
  if (joinCount == 0) return from;
  int8_t idx = -1;
  for (uint8_t i = 0; i < joinCount; i++) if (joinOrder[i] == from) { idx = i; break; }
  if (idx < 0) return joinOrder[0];
  return joinOrder[(idx + 1) % joinCount];
}

void joinOrderAdd(int8_t s) {
  for (uint8_t i = 0; i < joinCount; i++) if (joinOrder[i] == s) return;
  if (joinCount < NUM_SIDES) joinOrder[joinCount++] = s;
}

void joinOrderRemove(int8_t s) {
  for (uint8_t i = 0; i < joinCount; i++) {
    if (joinOrder[i] == s) {
      for (uint8_t j = i; j + 1 < joinCount; j++) joinOrder[j] = joinOrder[j + 1];
      joinCount--;
      return;
    }
  }
}

// Rebuild join order from the active roster in physical order (used when the
// tapped-in order isn't available: an empty-exit restore, or invalid NVS).
void rebuildJoinOrderFromRoster() {
  joinCount = 0;
  for (uint8_t s = 0; s < NUM_SIDES; s++) if (sideActive[s]) joinOrder[joinCount++] = s;
}

// Restore the ARB join order from NVS, validating against the active roster;
// falls back to physical order if missing or inconsistent.
void loadJoinOrder() {
  int8_t stored[NUM_SIDES];
  uint8_t n = prefs.getUChar("ordn", 0);
  bool ok = (n <= NUM_SIDES) && (prefs.getBytes("order", stored, NUM_SIDES) == (size_t)NUM_SIDES);
  uint8_t seen = 0;
  for (uint8_t i = 0; i < n && ok; i++) {
    int8_t s = stored[i];
    if (s < 0 || s >= NUM_SIDES || !sideActive[s] || (seen & (1 << s))) ok = false;
    else seen |= (1 << s);
  }
  if (ok && seen == rosterMask()) {
    memcpy(joinOrder, stored, NUM_SIDES);
    joinCount = n;
  } else {
    rebuildJoinOrderFromRoster();
  }
}

bool readyMode() { return gameMode == MODE_READY; }

// Only the current seat lights, in that side's fixed color; every other seat dark.
void renderTurn() {
  if (currentSide >= 0 && sideActive[currentSide]) {
    showOnlySide(currentSide, PLAYER_COLORS[currentSide]);
  } else {
    renderOff();
  }
}

// Phase 1: the whole table acts out the candidate mode, ~5 s per mode, so
// nobody has to remember a color legend — the animation IS the mode.
void renderModeDemo(uint32_t now) {
  if (now - demoModeStartMs >= MODE_DEMO_MS) {
    demoModeStartMs = now;
    dialMode = (dialMode + 1) % MODE_COUNT;
    Serial.printf("Setup: demoing %s\n", MODE_NAMES[dialMode]);
  }
  uint32_t elapsed = now - demoModeStartMs;

  FastLED.clear();
  switch (dialMode) {
    case MODE_CW:
    case MODE_CCW: {  // twin snakes lapping the table from opposite sides
      uint16_t total = totalLeds();
      for (uint8_t snake = 0; snake < 2; snake++) {
        uint16_t head = ((elapsed / DEMO_SNAKE_MS) + snake * (total / 2)) % total;
        for (uint8_t k = 0; k < DEMO_SNAKE_LEN; k++) {
          // CW: heads climb the strip (same direction as nextActiveSide), tails
          // trailing below them; CCW is the mirror image.
          uint16_t pos = (dialMode == MODE_CW)
                           ? (head + total - k) % total
                           : (total - 1 - head + k) % total;
          CRGB col = MODE_COLORS[dialMode];
          col.nscale8(255 - (uint16_t)k * 255 / DEMO_SNAKE_LEN);
          leds[pos] = col;
        }
      }
      break;
    }
    case MODE_ARB: {  // the turn lands solid on one seat, then another — every
                      // seat exactly once per round, in a freshly shuffled order
      static uint8_t  order[NUM_SIDES] = {0, 1, 2, 3, 4, 5, 6, 7};
      static uint8_t  orderPos = NUM_SIDES - 1;  // wraps on the first tick, so round one is shuffled too
      static uint32_t lastHopMs = 0;             // 0 = that first tick fires immediately
      if (now - lastHopMs >= DEMO_HOP_MS) {
        lastHopMs = now;
        if (++orderPos >= NUM_SIDES) {
          orderPos = 0;
          uint8_t last = order[NUM_SIDES - 1];
          do {  // Fisher-Yates; re-deal if the new round would open on the seat
                // that just closed the old one (a repeat the eye would catch)
            for (uint8_t i = NUM_SIDES - 1; i > 0; i--) {
              uint8_t j = random(i + 1);
              uint8_t t = order[i]; order[i] = order[j]; order[j] = t;
            }
          } while (order[0] == last);
        }
      }
      fillSide(order[orderPos], MODE_COLORS[MODE_ARB]);
      break;
    }
    default: {  // MODE_READY: seats flip green one by one, hold all-green, snap back
      uint8_t greens = (elapsed % ((uint32_t)NUM_SIDES * DEMO_FLIP_MS + DEMO_HOLD_MS)) / DEMO_FLIP_MS;
      if (greens > NUM_SIDES) greens = NUM_SIDES;
      for (uint8_t s = 0; s < NUM_SIDES; s++) {
        fillSide(s, s < greens ? READY_GREEN : READY_RED);
      }
      break;
    }
  }
  FastLED.show();
}

// Phase 2: joined seats blink in their colors; empty seats dark.
void renderJoin() {
  static uint32_t lastBlink = 0;
  static bool blinkState = false;

  uint32_t now = millis();
  if (now - lastBlink > 400) {
    lastBlink = now;
    blinkState = !blinkState;
  }

  FastLED.clear();
  for (uint8_t s = 0; s < NUM_SIDES; s++) {
    if (sideActive[s] && blinkState) fillSide(s, PLAYER_COLORS[s]);
  }
  FastLED.show();
}

// READY mode: every active seat is always lit — red until its player taps it
// green. All-green holds until any tap resets the round; empty seats stay dark.
void renderReady() {
  FastLED.clear();
  for (uint8_t s = 0; s < NUM_SIDES; s++) {
    if (sideActive[s]) fillSide(s, ready[s] ? READY_GREEN : READY_RED);
  }
  FastLED.show();
}

// Redraw whatever should be on screen, without changing any game state. This is
// the safe repaint: startPlay() clears ready[], which would wipe a READY round
// in progress if it were used for a brightness or power change.
void renderCurrent() {
  if (!tableLit) { renderOff(); return; }
  if (inSetupMode) return;            // setup re-renders from loop() every pass
  if (readyMode()) renderReady(); else renderTurn();
}

// Start (or resume) play in the current mode.
void startPlay() {
  if (!tableLit) { renderOff(); return; }
  if (readyMode()) {
    for (uint8_t s = 0; s < NUM_SIDES; s++) ready[s] = false;
    renderReady();
  } else {
    renderTurn();
  }
}

void advanceTurn() {
  switch (gameMode) {
    case MODE_CCW: currentSide = prevActiveSide(currentSide); break;
    case MODE_ARB: currentSide = nextInJoinOrder(currentSide); break;
    default:       currentSide = nextActiveSide(currentSide); break;  // MODE_CW
  }
  prefs.putUChar("curside", currentSide);
}

// Trips only on SETUP_TAP_COUNT taps on the SAME side within the window. Tracking
// the side matters for READY mode, where several players tap in quick succession —
// counting taps alone would false-trip setup. A tap on a different side restarts
// the burst there.
bool registerTapForSetupGesture(int8_t side, uint32_t now) {
  if (firstTapInBurstMs == 0 || now - firstTapInBurstMs > SETUP_TAP_WINDOW_MS || side != burstSide) {
    firstTapInBurstMs = now;
    burstSide = side;
    tapsInBurst = 1;
    return false;
  }
  tapsInBurst++;
  return tapsInBurst >= SETUP_TAP_COUNT;
}

void enterSetupMode() {
  inSetupMode = true;
  setupPhase = PHASE_MODE;
  firstTapInBurstMs = 0;
  tapsInBurst = 0;

  // Snapshot everything a phase-1 abort (or an empty phase-2 exit) must put back.
  prevRosterMask = rosterMask();
  memcpy(prevJoinOrder, joinOrder, NUM_SIDES);
  prevJoinCount = joinCount;

  for (uint8_t s = 0; s < NUM_SIDES; s++) sideActive[s] = false;  // everyone taps in fresh — nobody auto-joins
  joinCount = 0;

  dialMode = gameMode;            // demos start from the active mode
  setupEnteredMs = millis();
  demoModeStartMs = setupEnteredMs;

  Serial.printf("Entering setup - demoing %s; tap any side to pick the mode showing\n", MODE_NAMES[dialMode]);
}

// Phase-1 idle timeout: nobody picked a mode, so put the table back exactly as
// it was — roster, join order and mode untouched, nothing written to NVS.
void abortSetupMode() {
  inSetupMode = false;
  applyRosterMask(prevRosterMask);
  memcpy(joinOrder, prevJoinOrder, NUM_SIDES);
  joinCount = prevJoinCount;
  if (activeCount() == 0) {
    applyRosterMask(0xFF);
    rebuildJoinOrderFromRoster();
  }
  Serial.println("Setup abort: no mode picked, nothing changed");
}

void exitSetupMode() {
  inSetupMode = false;
  if (activeCount() == 0) {
    applyRosterMask(prevRosterMask);                 // nobody joined — restore the previous roster, don't brick
    memcpy(joinOrder, prevJoinOrder, NUM_SIDES);     // and the join order that goes with it
    joinCount = prevJoinCount;
    if (activeCount() == 0) {
      applyRosterMask(0xFF);                         // previous was empty too — fall back to all seats in
      rebuildJoinOrderFromRoster();
    }
    Serial.println("Setup exit: no seats joined, roster restored");
  }
  // ARB starts on the first joiner; other modes at the lowest active seat.
  currentSide = (gameMode == MODE_ARB && joinCount > 0) ? joinOrder[0] : firstActiveSide();

  prefs.putUChar("roster", rosterMask());
  prefs.putUChar("curside", currentSide);
  prefs.putUChar("mode", gameMode);
  prefs.putBytes("order", joinOrder, NUM_SIDES);
  prefs.putUChar("ordn", joinCount);
  Serial.printf("Setup done. %d seats, mode=%s, starting at side %d\n", activeCount(), MODE_NAMES[gameMode], currentSide);
}

// The only ways to change mode, brightness and power from outside the tap flow.
// Each returns false only for "valid but refused right now" — range checking is
// the caller's job, so the web layer can tell 400 from 409. Defined here, below
// abortSetupMode() and above commitTap(), because it sits between its callee and
// its caller.

bool applyMode(uint8_t newMode) {
  if (newMode >= MODE_COUNT) return false;
  if (inSetupMode) return false;   // someone is mid-gesture at the table; don't
                                   // yank the mode out from under the demo dial
  gameMode = newMode;
  if (gameMode == MODE_ARB && joinCount == 0) rebuildJoinOrderFromRoster();
  if (currentSide < 0 || currentSide >= NUM_SIDES || !sideActive[currentSide]) {
    currentSide = (gameMode == MODE_ARB && joinCount > 0) ? joinOrder[0] : firstActiveSide();
  }
  prefs.putUChar("mode", gameMode);
  prefs.putUChar("curside", currentSide);
  startPlay();                     // a mode change SHOULD clear ready[]
  Serial.printf("Mode set to %s\n", MODE_NAMES[gameMode]);
  return true;
}

bool applyBrightness(uint8_t pct) {
  setBrightnessPercent(pct);       // brightnessTick() eases the strip toward it and
  return true;                     // issues the show(), so no repaint is needed here
}

bool applyPower(bool lit) {
  if (lit == tableLit) return true;
  if (!lit && inSetupMode) {
    abortSetupMode();              // restores the previous roster and mode, writes nothing
  }
  tableLit = lit;
  if (lit) renderCurrent(); else renderOff();   // NOT startPlay() — that would
                                                // clear a READY round's greens
  Serial.printf("Table %s\n", lit ? "on" : "off");
  return true;
}

void commitTap(int8_t side, uint32_t whenMs) {
  if (!tableLit) {
    // Dark: the only gesture that does anything is the wake burst — four fast
    // taps on one side, the same burst that opens setup while lit. The two
    // states are mutually exclusive, so there's no ambiguity. Single taps stay
    // inert, so bumping the table doesn't relight it.
    if (registerTapForSetupGesture(side, whenMs)) {
      firstTapInBurstMs = 0;   // consume the burst, so waking never also opens
      tapsInBurst = 0;         // setup or passes a turn
      burstSide = -1;
      applyPower(true);
    }
    return;
  }

  if (inSetupMode) {
    if (setupPhase == PHASE_MODE) {
      // Any side commits the mode being demoed — but not the tail of the entry
      // gesture (a stray 5th tap, or its cross-talk ghost on another side).
      if (whenMs - setupEnteredMs < MODE_TAP_GRACE_MS) return;
      gameMode = dialMode;
      setupPhase = PHASE_JOIN;
      Serial.printf("Setup: mode = %s - tap seats to join\n", MODE_NAMES[gameMode]);
      return;
    }
    sideActive[side] = !sideActive[side];  // toggle this seat in/out of the roster
    if (sideActive[side]) joinOrderAdd(side); else joinOrderRemove(side);
    Serial.printf("Setup: side %d %s\n", side, sideActive[side] ? "IN" : "out");
    return;
  }

  if (readyMode()) {
    // Group ready-check: no current seat, so any fast 4-tap opens setup.
    if (registerTapForSetupGesture(side, whenMs)) {
      enterSetupMode();
      return;
    }
    if (!sideActive[side]) return;             // seats not in the roster do nothing
    bool allReady = true;
    for (uint8_t s = 0; s < NUM_SIDES; s++) {
      if (sideActive[s] && !ready[s]) { allReady = false; break; }
    }
    if (allReady) {
      // Whole table green: the round is over — any tap deals the next one.
      for (uint8_t s = 0; s < NUM_SIDES; s++) ready[s] = false;
      Serial.println("Ready: round reset - everyone back to red");
    } else {
      ready[side] = !ready[side];
      Serial.printf("Ready: side %d %s\n", side, ready[side] ? "GREEN" : "red");
    }
    renderReady();
    return;
  }

  // Turn-passing modes (CW / CCW / ARB).
  if (side == currentSide) {
    advanceTurn();
    renderTurn();
  } else {
    // Setup gesture only counts taps on a non-current side. Turn-passes always
    // land on the current seat, so brisk normal play can never accumulate toward
    // the gesture and trip setup mode by accident.
    if (registerTapForSetupGesture(side, whenMs)) {
      enterSetupMode();
      return;
    }
    Serial.printf("Tap on side %d ignored - not the current seat\n", side);
  }
}

// Fills the snapshot the phone page renders from. READY has no single current
// seat, so it reports -1 rather than a stale one.
void readTableState(TableState &s) {
  s.mode              = gameMode;
  s.brightnessPercent = brightnessPercent();
  s.lit               = tableLit;
  s.currentSide       = readyMode() ? -1 : currentSide;
  s.rosterMask        = rosterMask();
  s.readyMask         = 0;
  for (uint8_t i = 0; i < NUM_SIDES; i++) if (ready[i]) s.readyMask |= (1 << i);
  s.inSetupMode       = inSetupMode;
}

const TableConfig WEB_CONFIG = {MODE_NAMES, MODE_COUNT};

bool wifiConfigured() { return WIFI_SSID[0] != '\0'; }

// Installs the OTA handlers and opens the listener. Separate from the connect so
// it can run the first time the link comes up, however late that is.
void beginOta() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    otaActive = true;
    FastLED.clear();
    FastLED.show();
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    uint8_t percent = (uint32_t)progress * 100 / total;
    renderProgressBar(percent, CRGB(0, 80, 255));
  });

  ArduinoOTA.onEnd([]() {
    fill_solid(leds, totalLeds(), CRGB(0, 200, 0));
    FastLED.show();
    delay(500);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    fill_solid(leds, totalLeds(), CRGB(255, 0, 0));
    FastLED.show();
    delay(2000);
    otaActive = false;
  });

  ArduinoOTA.begin();
  webUiBegin(WEB_CONFIG, readTableState, applyMode, applyBrightness, applyPower);
  netServicesUp = true;
  Serial.printf("OTA ready at %s.local (", OTA_HOSTNAME);
  Serial.print(WiFi.localIP());
  Serial.println(")");
}

void setupWiFi() {
  if (!wifiConfigured()) {
    Serial.println("No secrets.h (or empty SSID) - WiFi and OTA disabled");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttemptMs = millis();

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());
    Serial.printf("MAC %s (use this for a DHCP reservation)\n", WiFi.macAddress().c_str());
    beginOta();
  } else {
    Serial.println("WiFi not up yet - retrying in the background, OTA starts when it joins");
  }
}

// Non-blocking link maintenance. Before this existed a slow AP at boot meant no
// OTA until the table was power-cycled. Never delays, so tap latency is
// unaffected.
void serviceWiFi(uint32_t now) {
  if (!wifiConfigured()) return;

  if (WiFi.status() == WL_CONNECTED) {
    if (!netServicesUp) beginOta();
    return;
  }

  if (netServicesUp) {       // link dropped — tear both down so the reconnect
    ArduinoOTA.end();        // rebinds them against the new address
    webUiEnd();
    netServicesUp = false;
    Serial.println("WiFi lost - OTA and web UI offline until it returns");
  }

  if (now - lastWifiAttemptMs < WIFI_RETRY_MS) return;
  lastWifiAttemptMs = now;
  WiFi.reconnect();
}

// Bench commands over USB serial: m remaps the piezos, p prints the map. The
// wizard is blocking and owns the LEDs, so any setup session in progress is
// dropped and play restarts cleanly when it returns.
void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'm') {
      runPiezoMapWizard();
      inSetupMode = false;
      startPlay();
    } else if (c == 'p') {
      printPiezoMap();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  prefs.begin("turntable", false);
  applyRosterMask(prefs.getUChar("roster", 0xFF));   // default: all 8 seats in
  currentSide = prefs.getUChar("curside", 0);
  if (activeCount() == 0) applyRosterMask(0xFF);
  if (currentSide < 0 || currentSide >= NUM_SIDES || !sideActive[currentSide]) currentSide = firstActiveSide();

  gameMode = prefs.getUChar("mode", MODE_CW);
  if (gameMode >= MODE_COUNT) gameMode = MODE_CW;
  loadJoinOrder();                                   // restore ARB turn order (or rebuild from roster)

  octagonBegin();                    // side table from NVS, FastLED, piezo baselines
  // No pair handler: power is physical (plug in = on), and skipping the pair
  // hold means taps commit the scan they're detected instead of 150 ms later.
  tapsBegin(commitTap, nullptr);

  setupWiFi();

  startPlay();
}

void loop() {
  if (netServicesUp) {
    ArduinoOTA.handle();
    webUiHandle();
  }

  if (otaActive) {
    delay(5);
    return;
  }

  handleSerial();

  uint32_t now = millis();

  serviceWiFi(now);
  brightnessTick(now);

  tapsPoll(now);

  if (inSetupMode) {
    // lastAnyTapMs() is never 0 here — setup can only be entered by tapping.
    if (setupPhase == PHASE_MODE) {
      if (now - lastAnyTapMs() > MODE_ABORT_IDLE_MS) {
        abortSetupMode();
        startPlay();
      } else {
        renderModeDemo(now);
      }
    } else if (now - lastAnyTapMs() > SETUP_JOIN_IDLE_MS) {
      exitSetupMode();
      startPlay();
    } else {
      renderJoin();
    }
  }

  delay(5);
}
