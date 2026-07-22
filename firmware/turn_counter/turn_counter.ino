#include <FastLED.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>

// Targets ESP32-S3 (e.g. ESP32-S3-DevKitC-1 N8R8). Piezos live on ADC1
// (GPIO 1-10) so they keep working while Wi-Fi/BLE are active.
#define LED_PIN          11
#define NUM_LEDS         240   // buffer ceiling; the lit total is sideStarts[NUM_SIDES]
#define NUM_SIDES        8
#define BRIGHTNESS       128
#define MAX_POWER_MA     1500  // FastLED auto-dims to hold LED draw here. Sized so the whole table
                               // can run through the ESP32-S3 dev board's own USB jack: the board's
                               // 5V path is good for ~1.5-2A, and 1500mA LEDs + ~250mA ESP ~= 1.75A.
                               // One-lit-side play (~0.6A) never hits the cap and stays full-bright;
                               // only the all-on moments (setup blink, READY, ready-flash) dim (~1/3).
                               // For full-brightness all-on, feed the strip 5V directly (not through
                               // the board) and raise this to ~2500 — see design doc 3.5.
#define LED_TYPE         WS2812B
#define COLOR_ORDER      GRB

const char* WIFI_SSID     = "your-network-here";
const char* WIFI_PASSWORD = "your-password-here";
const char* OTA_HOSTNAME  = "turn-counter";
const char* OTA_PASSWORD  = "letsplayagame";

const uint32_t WIFI_CONNECT_TIMEOUT_MS = 5000;

// All 8 piezos on ADC1. GPIO 3 is skipped because it's a JTAG strap pin.
const uint8_t PIEZO_PINS[NUM_SIDES] = {1, 2, 4, 5, 6, 7, 8, 9};

// Adaptive tap detection (same approach as tap_light.ino): each side's resting
// ADC level is averaged at boot, then tracked with a slow moving average. A tap
// fires when a reading jumps TAP_DELTA above that side's own baseline. Spike
// readings never feed the average, so taps don't desensitize a side, while slow
// drift (temperature, glue aging, ambient vibration) is absorbed automatically.
const uint16_t TAP_DELTA                = 1000;
const uint16_t DEBOUNCE_MS              = 250;
const uint8_t  SETUP_TAP_COUNT          = 4;
const uint16_t SETUP_TAP_WINDOW_MS      = 2000;
const uint16_t SETUP_EXIT_IDLE_MS       = 3000;
const uint16_t OPPOSITE_PAIR_WINDOW_MS  = 150;
const uint16_t MODE_DIAL_MS             = 5000;   // dial shows each mode ~5 s while browsing
const uint16_t SETUP_BROWSE_IDLE_MS     = 20000;  // dial unlocked: longer idle before auto-commit

// Interaction modes chosen on the setup dial. CW/CCW/ARB are turn-passing
// variants; READY is a group ready-check with no single "current" seat.
enum GameMode : uint8_t { MODE_CW = 0, MODE_CCW = 1, MODE_ARB = 2, MODE_READY = 3, MODE_COUNT = 4 };

// Per-side LED counts. Corner cuts eat 1-2 LEDs unpredictably, so the octagon's
// sides aren't uniform — this table is the source of truth for where each side
// starts. Defaults are overridden at boot by a table calibrated from the
// tap_light firmware (NVS namespace "octagon", serial commands 0-7/+/-/p).
uint8_t sideLedCounts[NUM_SIDES] = {29, 28, 27, 27, 27, 28, 28, 27};  // calibrated 2026-07-21, 221 LEDs total
uint16_t sideStarts[NUM_SIDES + 1];  // prefix sums; sideStarts[NUM_SIDES] = lit total

CRGB leds[NUM_LEDS];
Preferences prefs;      // "turntable": game state
Preferences sidePrefs;  // "octagon": calibrated side table

bool     sideActive[NUM_SIDES] = {true, true, true, true, true, true, true, true};  // roster: which seats are "in"
int8_t   currentSide = 0;        // the active seat whose turn it is (index into sideActive)
uint8_t  prevRosterMask = 0xFF;  // roster snapshot taken on setup entry; restored if you exit with 0 seats joined
uint32_t lastTapPerSide[NUM_SIDES] = {0};
uint32_t baselineAcc[NUM_SIDES] = {0};  // fixed-point moving averages of resting levels, scaled by 64
uint32_t lastAnyTapMs = 0;
bool     inSetupMode = false;
bool     isOn = true;
bool     otaActive = false;

uint8_t  gameMode = MODE_CW;            // persisted interaction mode
int8_t   joinOrder[NUM_SIDES] = {0};    // seats in the order they joined (ARB turn order)
uint8_t  joinCount = 0;
int8_t   setupSide = -1;                // side that opened setup: the mode dial, auto-joined
bool     modeLocked = true;             // dial locked (solid, committed) vs. cycling to browse (flashing)
uint8_t  dialMode = MODE_CW;            // mode currently shown on the dial
uint32_t dialLastAdvanceMs = 0;         // last time the dial auto-advanced while unlocked
bool     ready[NUM_SIDES] = {false};    // READY mode: which active seats are lit on

uint32_t firstTapInBurstMs = 0;
uint8_t  tapsInBurst = 0;
int8_t   burstSide = -1;   // the side the current tap-burst is accumulating on

int8_t   pendingTapSide = -1;
uint32_t pendingTapMs = 0;

const CRGB PLAYER_COLORS[8] = {
  CRGB(255, 40, 40),
  CRGB(40, 120, 255),
  CRGB(60, 220, 80),
  CRGB(255, 200, 30),
  CRGB(220, 60, 220),
  CRGB(40, 230, 230),
  CRGB(255, 130, 30),
  CRGB(180, 180, 220)
};

// Shown only on the mode dial (the setup side) during setup.
const CRGB MODE_COLORS[MODE_COUNT] = {
  CRGB(60, 220, 80),    // CW    - green
  CRGB(40, 120, 255),   // CCW   - blue
  CRGB(220, 60, 220),   // ARB   - magenta
  CRGB(255, 130, 30)    // READY - orange
};
const char* const MODE_NAMES[MODE_COUNT] = {
  "clockwise", "counter-clockwise", "arbitrary (join order)", "ready-or-not"
};

uint16_t baseline(uint8_t i) { return baselineAcc[i] >> 6; }

void rebuildSideStarts() {
  sideStarts[0] = 0;
  for (uint8_t s = 0; s < NUM_SIDES; s++) {
    sideStarts[s + 1] = sideStarts[s] + sideLedCounts[s];
  }
}

uint16_t totalLeds() { return sideStarts[NUM_SIDES]; }

bool validSideTable(const uint8_t *t) {
  uint16_t total = 0;
  for (uint8_t s = 0; s < NUM_SIDES; s++) {
    if (t[s] < 1 || t[s] > 60) return false;
    total += t[s];
  }
  return total <= NUM_LEDS;
}

void loadSideTable() {
  uint8_t stored[NUM_SIDES];
  if (sidePrefs.getBytes("sides", stored, NUM_SIDES) == NUM_SIDES && validSideTable(stored)) {
    memcpy(sideLedCounts, stored, NUM_SIDES);
    Serial.println("Loaded calibrated side table from NVS");
  }
  rebuildSideStarts();
  Serial.print("Side LED counts:");
  for (uint8_t s = 0; s < NUM_SIDES; s++) Serial.printf(" %u", sideLedCounts[s]);
  Serial.printf(" (total %u)\n", totalLeds());
}

bool isOppositeSide(int8_t a, int8_t b) {
  if (a < 0 || b < 0) return false;
  return ((a + NUM_SIDES / 2) % NUM_SIDES) == b;
}

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
  FastLED.clear();
  if (currentSide >= 0 && sideActive[currentSide]) {
    fill_solid(&leds[sideStarts[currentSide]], sideLedCounts[currentSide], PLAYER_COLORS[currentSide]);
  }
  FastLED.show();
}

void renderSetup() {
  static uint32_t lastBlink = 0;
  static bool blinkState = false;

  uint32_t now = millis();
  if (now - lastBlink > 400) {
    lastBlink = now;
    blinkState = !blinkState;
  }

  // While the dial is unlocked it auto-cycles the modes, ~5 s each.
  if (!modeLocked && now - dialLastAdvanceMs >= MODE_DIAL_MS) {
    dialLastAdvanceMs = now;
    dialMode = (dialMode + 1) % MODE_COUNT;
  }

  FastLED.clear();
  for (uint8_t s = 0; s < NUM_SIDES; s++) {
    if (s == setupSide) {
      // The mode dial: flashing while you browse (color cycles ~5 s each), solid
      // once you tap to commit a mode.
      if (modeLocked || blinkState) {
        fill_solid(&leds[sideStarts[s]], sideLedCounts[s], MODE_COLORS[dialMode]);
      }
    } else if (sideActive[s] && blinkState) {
      fill_solid(&leds[sideStarts[s]], sideLedCounts[s], PLAYER_COLORS[s]);
    }
  }
  FastLED.show();
}

void renderOff() {
  FastLED.clear();
  FastLED.show();
}

// READY mode: each active seat that has tapped in shows its own color; rest dark.
void renderReady() {
  FastLED.clear();
  for (uint8_t s = 0; s < NUM_SIDES; s++) {
    if (sideActive[s] && ready[s]) {
      fill_solid(&leds[sideStarts[s]], sideLedCounts[s], PLAYER_COLORS[s]);
    }
  }
  FastLED.show();
}

// Everyone's in: blink all active seats a few times, then clear for the next round.
void flashReadyAndReset() {
  for (uint8_t b = 0; b < 3; b++) {
    FastLED.clear();
    FastLED.show();
    delay(120);
    for (uint8_t s = 0; s < NUM_SIDES; s++) {
      if (sideActive[s]) fill_solid(&leds[sideStarts[s]], sideLedCounts[s], PLAYER_COLORS[s]);
    }
    FastLED.show();
    delay(120);
  }
  for (uint8_t s = 0; s < NUM_SIDES; s++) ready[s] = false;
  renderReady();
}

// Start (or resume) play in the current mode.
void startPlay() {
  if (readyMode()) {
    for (uint8_t s = 0; s < NUM_SIDES; s++) ready[s] = false;
    renderReady();
  } else {
    renderTurn();
  }
}

void renderOtaProgress(uint8_t percent) {
  FastLED.clear();
  uint16_t lit = (uint32_t)totalLeds() * percent / 100;
  for (uint16_t i = 0; i < lit; i++) {
    leds[i] = CRGB(0, 80, 255);
  }
  FastLED.show();
}

// Returns a bitmask of accepted hits in this scan. At most two bits set:
// the side with the biggest jump above its own baseline (cross-talk filter —
// adjacent ghosts lose to the real hit) and, if its diametrically-opposite
// side also spiked this scan, that one too (two-handed slap detected in a
// single scan, before debounce can lock it out). Cross-scan two-handed
// slaps still work via pendingTapSide in onTapDetected().
uint8_t readPiezos(uint32_t now) {
  uint8_t aboveThresholdMask = 0;
  uint8_t maxIdx = 0xFF;
  uint16_t maxDelta = 0;

  for (uint8_t i = 0; i < NUM_SIDES; i++) {
    uint16_t reading = analogRead(PIEZO_PINS[i]);
    if (reading > baseline(i) + TAP_DELTA) {
      aboveThresholdMask |= (1 << i);
      uint16_t delta = reading - baseline(i);
      if (delta > maxDelta) {
        maxDelta = delta;
        maxIdx = i;
      }
    } else {
      baselineAcc[i] += reading - baseline(i);  // slow average, tau ~0.3 s at 5 ms/loop
    }
  }

  if (maxIdx == 0xFF) return 0;
  if (now - lastTapPerSide[maxIdx] <= DEBOUNCE_MS) return 0;

  uint8_t resultMask = (1 << maxIdx);
  lastTapPerSide[maxIdx] = now;
  lastAnyTapMs = now;

  uint8_t oppIdx = (maxIdx + NUM_SIDES / 2) % NUM_SIDES;
  if ((aboveThresholdMask & (1 << oppIdx)) &&
      now - lastTapPerSide[oppIdx] > DEBOUNCE_MS) {
    resultMask |= (1 << oppIdx);
    lastTapPerSide[oppIdx] = now;
  }

  return resultMask;
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

void enterSetupMode(int8_t triggerSide) {
  inSetupMode = true;
  firstTapInBurstMs = 0;
  tapsInBurst = 0;
  prevRosterMask = rosterMask();  // remember the roster so an empty exit can restore it
  for (uint8_t s = 0; s < NUM_SIDES; s++) sideActive[s] = false;  // clear — each player taps their own seat in

  setupSide = triggerSide;        // the side that opened setup becomes the mode dial...
  sideActive[setupSide] = true;   // ...and auto-joins (always in)
  joinCount = 0;
  joinOrderAdd(setupSide);

  dialMode = gameMode;            // dial starts on the current mode and cycles; tap it to commit
  modeLocked = false;
  dialLastAdvanceMs = millis();

  Serial.printf("Entering setup - side %d is the mode dial (tap to change mode); tap other seats to join\n", setupSide);
}

void exitSetupMode() {
  inSetupMode = false;
  setupSide = -1;
  if (activeCount() == 0) {
    applyRosterMask(prevRosterMask);                 // nobody joined — restore the previous roster, don't brick
    if (activeCount() == 0) applyRosterMask(0xFF);   // previous was empty too — fall back to all seats in
    rebuildJoinOrderFromRoster();                    // tap order lost on restore — use physical order
    Serial.println("Setup exit: no seats joined, roster restored");
  }
  // ARB starts on the first joiner (the configurer); the others at the lowest active seat.
  currentSide = (gameMode == MODE_ARB && joinCount > 0) ? joinOrder[0] : firstActiveSide();

  prefs.putUChar("roster", rosterMask());
  prefs.putUChar("curside", currentSide);
  prefs.putUChar("mode", gameMode);
  prefs.putBytes("order", joinOrder, NUM_SIDES);
  prefs.putUChar("ordn", joinCount);
  Serial.printf("Setup done. %d seats, mode=%s, starting at side %d\n", activeCount(), MODE_NAMES[gameMode], currentSide);
}

void toggleOnOff() {
  isOn = !isOn;
  inSetupMode = false;
  setupSide = -1;
  firstTapInBurstMs = 0;
  tapsInBurst = 0;
  pendingTapSide = -1;
  prefs.putUChar("ison", isOn ? 1 : 0);

  if (isOn) {
    Serial.println("Power ON");
    startPlay();
  } else {
    Serial.println("Power OFF");
    renderOff();
  }
}

void commitTap(int8_t side, uint32_t whenMs) {
  if (!isOn) return;

  if (inSetupMode) {
    if (side == setupSide) {
      // The dial: tap to lock the shown mode, tap again to unlock and keep browsing.
      modeLocked = !modeLocked;
      if (modeLocked) {
        gameMode = dialMode;
        Serial.printf("Setup: mode locked = %s\n", MODE_NAMES[gameMode]);
      } else {
        dialLastAdvanceMs = millis();
        Serial.println("Setup: mode dial browsing");
      }
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
      enterSetupMode(side);
      return;
    }
    if (!sideActive[side]) return;             // seats not in the roster do nothing
    ready[side] = !ready[side];                // toggle this seat's ready light
    Serial.printf("Ready: side %d %s\n", side, ready[side] ? "ON" : "off");
    bool allReady = true;
    for (uint8_t s = 0; s < NUM_SIDES; s++) {
      if (sideActive[s] && !ready[s]) { allReady = false; break; }
    }
    if (allReady) {
      Serial.println("Ready: everyone in - flashing, then resetting");
      flashReadyAndReset();
    } else {
      renderReady();
    }
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
      enterSetupMode(side);
      return;
    }
    Serial.printf("Tap on side %d ignored - not the current seat\n", side);
  }
}

void onTapDetected(int8_t side, uint32_t now) {
  if (pendingTapSide >= 0 && isOppositeSide(pendingTapSide, side)) {
    pendingTapSide = -1;
    toggleOnOff();
    return;
  }

  if (pendingTapSide >= 0) {
    commitTap(pendingTapSide, pendingTapMs);
  }

  pendingTapSide = side;
  pendingTapMs = now;
}

void setupWiFiAndOta() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(100);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, OTA disabled");
    return;
  }

  Serial.print("WiFi connected: ");
  Serial.println(WiFi.localIP());

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    otaActive = true;
    FastLED.clear();
    FastLED.show();
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    uint8_t percent = (uint32_t)progress * 100 / total;
    renderOtaProgress(percent);
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
  Serial.println("OTA ready");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  prefs.begin("turntable", false);
  applyRosterMask(prefs.getUChar("roster", 0xFF));   // default: all 8 seats in
  currentSide = prefs.getUChar("curside", 0);
  isOn        = prefs.getUChar("ison", 1) ? true : false;
  if (activeCount() == 0) applyRosterMask(0xFF);
  if (currentSide < 0 || currentSide >= NUM_SIDES || !sideActive[currentSide]) currentSide = firstActiveSide();

  gameMode = prefs.getUChar("mode", MODE_CW);
  if (gameMode >= MODE_COUNT) gameMode = MODE_CW;
  loadJoinOrder();                                   // restore ARB turn order (or rebuild from roster)

  isOn = true;  // TEMP bench: force always-on so a stale NVS "off" doesn't boot dark — REVERT for real play

  sidePrefs.begin("octagon", false);
  loadSideTable();

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_POWER_MA);  // runs off the board's USB; dims all-on peaks

  for (uint8_t i = 0; i < NUM_SIDES; i++) {
    pinMode(PIEZO_PINS[i], INPUT);
  }

  // Seed each side's baseline with ~0.5 s of quiet readings before accepting taps.
  uint32_t sums[NUM_SIDES] = {0};
  for (uint8_t s = 0; s < 100; s++) {
    for (uint8_t i = 0; i < NUM_SIDES; i++) {
      sums[i] += analogRead(PIEZO_PINS[i]);
    }
    delay(5);
  }
  Serial.print("Piezo baselines:");
  for (uint8_t i = 0; i < NUM_SIDES; i++) {
    baselineAcc[i] = (sums[i] / 100) << 6;
    Serial.printf(" %u", baseline(i));
  }
  Serial.printf(" (tap fires at baseline + %u)\n", TAP_DELTA);

  setupWiFiAndOta();

  if (isOn) {
    startPlay();
  } else {
    renderOff();
  }
}

void loop() {
  ArduinoOTA.handle();

  if (otaActive) {
    delay(5);
    return;
  }

  uint32_t now = millis();

  uint8_t hits = readPiezos(now);
  for (uint8_t i = 0; i < NUM_SIDES; i++) {
    if (hits & (1 << i)) {
      onTapDetected(i, now);
    }
  }

  if (pendingTapSide >= 0 && now - pendingTapMs >= OPPOSITE_PAIR_WINDOW_MS) {
    commitTap(pendingTapSide, pendingTapMs);
    pendingTapSide = -1;
  }

  if (isOn && inSetupMode) {
    uint32_t idleLimit = modeLocked ? SETUP_EXIT_IDLE_MS : SETUP_BROWSE_IDLE_MS;
    if (lastAnyTapMs != 0 && now - lastAnyTapMs > idleLimit) {
      if (!modeLocked) { modeLocked = true; gameMode = dialMode; }  // browsing timed out — lock what's shown
      exitSetupMode();
      startPlay();
    } else {
      renderSetup();
    }
  }

  delay(5);
}
