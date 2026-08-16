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
const uint16_t MODE_TAP_GRACE_MS        = 500;    // swallow gesture spillover right after entry
const uint16_t SETUP_JOIN_IDLE_MS       = 5000;   // phase 2: idle commits the roster
const uint16_t REFUSE_BLINK_MS          = 120;    // locked: on/off/on/off = 480 ms total
const CRGB     REFUSE_AMBER             = CRGB(255, 130, 0);

const uint16_t TIMER_SECONDS_DEFAULT    = 60;
const uint16_t TIMER_SECONDS_MIN        = 10;
const uint16_t TIMER_SECONDS_MAX        = 300;    // 3 digits: parseUInt in web_ui.cpp
                                                  // rejects anything longer
const uint16_t TIMER_SETTLE_MS          = 2000;   // defer the NVS write past a slider drag
const uint16_t MODE_FRAME_MS            = 50;     // timed modes repaint at 20 fps

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
// MODE_DIAL_COUNT is what the table's setup dial can demo; MODE_COUNT is what
// applyMode() accepts. MODE_DIAL_COUNT and MODE_TIMER deliberately share the
// value 4: one is a count, one is an index, and tying them together is the
// point — the dial demos [0, MODE_DIAL_COUNT) and the phone-only modes begin
// exactly where it stops. A mode the dial can't animate has no business on a
// dial whose whole premise is "the animation IS the mode".
enum GameMode : uint8_t {
  MODE_CW = 0, MODE_CCW = 1, MODE_ARB = 2, MODE_READY = 3,
  MODE_DIAL_COUNT = 4,
  MODE_TIMER = 4, MODE_SHARE = 5,
  MODE_COUNT = 6
};

// One full rotation, derived rather than hardcoded. The old fixed 25000 against
// a 5000 ms demo ran five demos across four modes, so the dial wrapped and
// replayed its opening mode in full before giving up. uint32_t because a demo
// longer than 16 s would silently wrap a uint16_t.
const uint32_t MODE_ABORT_IDLE_MS = (uint32_t)MODE_DEMO_MS * MODE_DIAL_COUNT;

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
bool     setupLocked = false;       // refuses the four-tap gesture while lit. Never persisted
                                    // either, and for a sharper reason: a locked table with no
                                    // Wi-Fi has no phone and no gesture, so unplug-replug has
                                    // to be the way out.
int8_t   refuseSide  = -1;          // side mid-refusal-flash, -1 = idle
uint32_t refuseStart = 0;

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

uint16_t turnSeconds = TIMER_SECONDS_DEFAULT;   // persisted, "turntable"/"tsec"
bool     turnSecondsDirty = false;
uint32_t turnSecondsChangedMs = 0;

uint32_t turnStartMs = 0;              // when the current seat's turn began
uint32_t sideMs[NUM_SIDES] = {0};      // banked play time per seat. RAM only:
                                       // this is the game, not a setting
uint32_t modeFrameMs = 0;              // last timed-mode repaint
int16_t  lastDrawnLeds = -1;           // suppresses redundant show(); -1 = repaint

uint32_t firstTapInBurstMs = 0;
uint8_t  tapsInBurst = 0;
int8_t   burstSide = -1;   // the side the current tap-burst is accumulating on

// Demo colors for the mode-select phase; READY demos in red/green instead.
const CRGB MODE_COLORS[MODE_DIAL_COUNT] = {   // demo-only, so it tracks the dial
  CRGB(60, 220, 80),    // CW    - green
  CRGB(40, 120, 255),   // CCW   - blue
  CRGB(220, 60, 220),   // ARB   - magenta
  CRGB(255, 130, 30)    // READY - orange
};
const char* const MODE_NAMES[MODE_COUNT] = {
  "clockwise", "counter-clockwise", "arbitrary (join order)", "ready-or-not",
  "countdown timer", "time share"
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
bool timedMode() { return gameMode == MODE_TIMER || gameMode == MODE_SHARE; }

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
    dialMode = (dialMode + 1) % MODE_DIAL_COUNT;
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

// Both timed modes draw a centred block that shrinks from the ends inward, so
// the last light left sits directly in front of the player. One visual grammar
// for both modes.
void drawCentredBlock(uint8_t side, uint16_t lit, const CRGB &col) {
  uint8_t len = sideLedCounts[side];
  if (lit > len) lit = len;
  FastLED.clear();
  uint16_t start = sideStarts[side] + (len - lit) / 2;
  for (uint16_t i = 0; i < lit; i++) leds[start + i] = col;
}

// Shot clock: the seat's own colour drains from both ends, warming toward red
// over the final quarter so "almost out" reads across a loud room without
// anyone counting LEDs. At zero it complains; it never takes the shot away.
void renderTimer(uint32_t now) {
  uint8_t  len     = sideLedCounts[currentSide];
  uint32_t total   = (uint32_t)turnSeconds * 1000;
  uint32_t elapsed = now - turnStartMs;

  if (elapsed >= total) {
    // beatsin8 runs off millis(), so the pulse keeps phase across repaints
    // without carrying any state of its own.
    FastLED.clear();
    fillSide(currentSide, CRGB(beatsin8(120, 40, 255), 0, 0));
    FastLED.show();
    lastDrawnLeds = -1;          // pulsing: every frame differs
    return;
  }

  uint32_t left = total - elapsed;
  uint16_t lit  = (uint32_t)len * left / total;
  uint32_t warn = total / 4;

  CRGB col = PLAYER_COLORS[currentSide];
  bool warming = left < warn;
  if (warming) col = blend(col, CRGB(255, 0, 0), 255 - (uint8_t)(left * 255 / warn));

  // Outside the final quarter nothing changes between LED steps, so skip the
  // ~6.6 ms show(). Inside it the colour moves continuously, so repaint.
  if (!warming && lit == lastDrawnLeds) return;
  lastDrawnLeds = lit;
  drawCentredBlock(currentSide, lit, col);
  FastLED.show();
}

// Your share of table time, measured against a fair share of HALF the side — so
// an even table sits half-lit and the bar has headroom both ways. It grows while
// you sit there and is shorter when the turn comes back, because everyone else's
// play diluted it.
void renderShare(uint32_t now) {
  uint8_t  len   = sideLedCounts[currentSide];
  uint32_t live  = now - turnStartMs;
  uint32_t mine  = sideMs[currentSide] + live;
  uint32_t total = live;
  for (uint8_t s = 0; s < NUM_SIDES; s++) total += sideMs[s];

  uint8_t n = activeCount();
  if (n == 0) n = 1;

  uint16_t lit;
  if (total == 0) {
    lit = len / 2;                 // nobody has played: open neutral
  } else {
    // 64-bit intermediate: mine * n * len overflows uint32_t at ~5 h on one
    // seat, and the bar would wrap to nonsense at the end of a long night —
    // the worst possible time to find it.
    lit = (uint16_t)(((uint64_t)mine * n * len) / ((uint64_t)2 * total));
    if (lit > len) lit = len;
    if (lit < 1) lit = 1;          // zero is indistinguishable from a dead piezo
  }

  if (lit == lastDrawnLeds) return;
  lastDrawnLeds = lit;
  drawCentredBlock(currentSide, lit, PLAYER_COLORS[currentSide]);
  FastLED.show();
}

// Draws immediately, with no frame-interval guard, so renderCurrent() can force
// a repaint. modeTick() is the throttled caller.
void renderTimed(uint32_t now) {
  if (currentSide < 0 || currentSide >= NUM_SIDES) { renderOff(); return; }
  if (gameMode == MODE_TIMER) renderTimer(now); else renderShare(now);
}

// Timed modes animate continuously — every other mode repaints only on a tap.
// FastLED.show() blocks ~6.6 ms on 221 LEDs, so the renderers skip it when
// nothing moved; setup mode already shows on every pass and taps register fine
// through it, so 20 fps is well inside proven territory.
void modeTick(uint32_t now) {
  if (!tableLit || inSetupMode || !timedMode()) return;
  if (now - modeFrameMs < MODE_FRAME_MS) return;
  modeFrameMs = now;
  renderTimed(now);
}

// Redraw whatever should be on screen, without changing any game state. This is
// the safe repaint: startPlay() clears ready[] and restarts the turn clock,
// either of which would be wrong for a brightness or power change.
void renderCurrent() {
  if (!tableLit) { renderOff(); return; }
  if (inSetupMode) return;            // setup re-renders from loop() every pass
  if (readyMode()) { renderReady(); return; }
  if (timedMode()) { lastDrawnLeds = -1; renderTimed(millis()); return; }
  renderTurn();
}

// A refused gesture has to say something, or it reads as a dead piezo. Runs as a
// state machine from loop() rather than a delay() in the tap handler, which would
// stall tapsPoll() and drag the adaptive baselines.
void refuseSetup(int8_t side, uint32_t now) {
  refuseSide = side;
  refuseStart = now;
  Serial.printf("Setup gesture on side %d refused - table is locked\n", side);
}

void refuseTick(uint32_t now) {
  if (refuseSide < 0) return;
  if (!tableLit || inSetupMode) { refuseSide = -1; return; }
  uint32_t phase = (now - refuseStart) / REFUSE_BLINK_MS;
  if (phase >= 4) {
    refuseSide = -1;
    renderCurrent();
    return;
  }
  if (phase % 2 == 0) {
    fillSide(refuseSide, REFUSE_AMBER);   // overlays the live scene, so no saved buffer
    FastLED.show();
  } else {
    renderCurrent();                      // safe repaint: does NOT clear ready[]
  }
}

// Start (or resume) play in the current mode. Unlike renderCurrent() this is a
// fresh start: the READY round is cleared and the turn clock restarts.
void startPlay() {
  if (!tableLit) { renderOff(); return; }
  turnStartMs = millis();
  lastDrawnLeds = -1;
  if (readyMode()) {
    for (uint8_t s = 0; s < NUM_SIDES; s++) ready[s] = false;
    renderReady();
  } else if (timedMode()) {
    renderTimed(millis());
  } else {
    renderTurn();
  }
}

// Time is banked on transitions, never accumulated per frame: a per-frame
// `sideMs[cur] += delta` drifts and depends on the loop rate. The live display
// adds (now - turnStartMs) on the fly instead.
void bankTurnTime() {
  uint32_t now = millis();
  if (currentSide >= 0 && currentSide < NUM_SIDES) {
    sideMs[currentSide] += now - turnStartMs;
  }
  turnStartMs = now;
}

// Time-share stats are the game, so they reset when a game starts.
void resetShareStats() {
  memset(sideMs, 0, sizeof(sideMs));
  turnStartMs = millis();
}

void advanceTurn() {
  bankTurnTime();   // credit the outgoing seat before currentSide moves
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

// True only when the burst completed AND setup is allowed to open. A refused
// burst is still consumed by registerTapForSetupGesture, so taps can't pile up
// against the lock and spring setup open the moment it's lifted.
bool setupGestureFired(int8_t side, uint32_t now) {
  if (!registerTapForSetupGesture(side, now)) return false;
  if (!setupLocked) return true;
  refuseSetup(side, now);
  return false;
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

  // Demos start from the active mode — unless that's a phone-only mode the dial
  // can't animate, which would also index MODE_COLORS out of bounds. Falling
  // back to CW gives the gesture a second job: it's the manual way out of a
  // phone-only mode when the phone isn't around.
  dialMode = (gameMode < MODE_DIAL_COUNT) ? gameMode : MODE_CW;
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

  resetShareStats();   // a new roster is a new game

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
  // Re-selecting SHARE from the phone is the "new game" gesture, so this fires
  // even when SHARE was already the active mode.
  if (gameMode == MODE_SHARE) resetShareStats();
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
  if (!lit) bankTurnTime();        // freeze the share clock where it stands
  tableLit = lit;
  if (lit) {
    // Relighting gives the current player a fresh turn clock — a shot clock that
    // expired in the dark isn't a shot clock. Share history survives; only the
    // in-progress turn restarts.
    turnStartMs = millis();
    lastDrawnLeds = -1;
    renderCurrent();               // NOT startPlay() — that would clear a
  } else {                         // READY round's greens
    renderOff();
  }
  Serial.printf("Table %s\n", lit ? "on" : "off");
  return true;
}

bool applyTurnSeconds(uint16_t secs) {
  turnSeconds = secs;
  turnSecondsDirty = true;
  turnSecondsChangedMs = millis();
  if (gameMode == MODE_TIMER) {
    // Restart rather than let the new length apply mid-turn: dropping 120 s to
    // 30 s would expire the turn instantly, which reads as a crash.
    turnStartMs = millis();
    lastDrawnLeds = -1;
    renderCurrent();
  }
  Serial.printf("Turn timer set to %u s\n", turnSeconds);
  return true;
}

// Mirrors brightnessTick's deferred write: dragging a slider costs one flash
// write instead of twenty.
void turnSecondsTick(uint32_t now) {
  if (!turnSecondsDirty || now - turnSecondsChangedMs < TIMER_SETTLE_MS) return;
  turnSecondsDirty = false;
  prefs.putUShort("tsec", turnSeconds);
  Serial.printf("Turn timer saved: %u s\n", turnSeconds);
}

bool applyLock(bool locked) {
  setupLocked = locked;               // deliberately not persisted — see the declaration
  Serial.printf("Setup %s\n", locked ? "locked" : "unlocked");
  return true;                        // never refused: locking mid-setup lets the session
}                                     // at the table finish rather than yanking it away

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
    if (setupGestureFired(side, whenMs)) {
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
    lastDrawnLeds = -1;   // new seat, new length — don't suppress the first frame
    renderCurrent();      // identical to renderTurn() for CW/CCW/ARB
  } else {
    // Setup gesture only counts taps on a non-current side. Turn-passes always
    // land on the current seat, so brisk normal play can never accumulate toward
    // the gesture and trip setup mode by accident.
    if (setupGestureFired(side, whenMs)) {
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
  s.locked            = setupLocked;
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
  const TableHooks hooks = {
    .read          = readTableState,
    .setMode       = applyMode,
    .setBrightness = applyBrightness,
    .setPower      = applyPower,
    .setLock       = applyLock,
  };
  webUiBegin(WEB_CONFIG, hooks);
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

  turnSeconds = prefs.getUShort("tsec", TIMER_SECONDS_DEFAULT);
  if (turnSeconds < TIMER_SECONDS_MIN || turnSeconds > TIMER_SECONDS_MAX) {
    turnSeconds = TIMER_SECONDS_DEFAULT;
  }
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
  refuseTick(now);
  modeTick(now);
  turnSecondsTick(now);

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
