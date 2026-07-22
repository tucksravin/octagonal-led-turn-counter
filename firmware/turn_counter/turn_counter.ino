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

uint32_t firstTapInBurstMs = 0;
uint8_t  tapsInBurst = 0;

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

  if (millis() - lastBlink > 400) {
    lastBlink = millis();
    blinkState = !blinkState;
  }

  FastLED.clear();
  if (blinkState) {
    for (uint8_t s = 0; s < NUM_SIDES; s++) {
      if (sideActive[s]) {
        fill_solid(&leds[sideStarts[s]], sideLedCounts[s], PLAYER_COLORS[s]);
      }
    }
  }
  FastLED.show();
}

void renderOff() {
  FastLED.clear();
  FastLED.show();
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
  currentSide = nextActiveSide(currentSide);
  prefs.putUChar("curside", currentSide);
}

bool registerTapForSetupGesture(uint32_t now) {
  if (firstTapInBurstMs == 0 || now - firstTapInBurstMs > SETUP_TAP_WINDOW_MS) {
    firstTapInBurstMs = now;
    tapsInBurst = 1;
    return false;
  }
  tapsInBurst++;
  return tapsInBurst >= SETUP_TAP_COUNT;
}

void enterSetupMode() {
  inSetupMode = true;
  firstTapInBurstMs = 0;
  tapsInBurst = 0;
  prevRosterMask = rosterMask();  // remember the roster so an empty exit can restore it
  for (uint8_t s = 0; s < NUM_SIDES; s++) sideActive[s] = false;  // clear — each player taps their own seat in
  Serial.println("Entering setup - roster cleared, tap each seat to join");
}

void exitSetupMode() {
  inSetupMode = false;
  if (activeCount() == 0) {
    applyRosterMask(prevRosterMask);                 // nobody joined — restore the previous roster, don't brick
    if (activeCount() == 0) applyRosterMask(0xFF);   // previous was empty too — fall back to all seats in
    Serial.println("Setup exit: no seats joined, roster restored");
  }
  currentSide = firstActiveSide();                   // start the turn at the lowest-numbered active seat
  prefs.putUChar("roster", rosterMask());
  prefs.putUChar("curside", currentSide);
  Serial.printf("Setup done. %d seats active, starting at side %d\n", activeCount(), currentSide);
}

void toggleOnOff() {
  isOn = !isOn;
  inSetupMode = false;
  firstTapInBurstMs = 0;
  tapsInBurst = 0;
  pendingTapSide = -1;
  prefs.putUChar("ison", isOn ? 1 : 0);

  if (isOn) {
    Serial.println("Power ON");
    renderTurn();
  } else {
    Serial.println("Power OFF");
    renderOff();
  }
}

void commitTap(int8_t side, uint32_t whenMs) {
  if (!isOn) return;

  if (inSetupMode) {
    sideActive[side] = !sideActive[side];  // toggle this seat in/out of the roster
    Serial.printf("Setup: side %d %s\n", side, sideActive[side] ? "IN" : "out");
    return;
  }

  if (side == currentSide) {
    advanceTurn();
    renderTurn();
  } else {
    // Setup gesture only counts taps on a non-current side. Turn-passes always
    // land on the current seat, so brisk normal play can never accumulate toward
    // the gesture and trip setup mode by accident.
    if (registerTapForSetupGesture(whenMs)) {
      enterSetupMode();
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

  isOn = true;  // TEMP bench: force always-on so a stale NVS "off" doesn't boot dark — REVERT for real play

  sidePrefs.begin("octagon", false);
  loadSideTable();

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);

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
    renderTurn();
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
    if (lastAnyTapMs != 0 && now - lastAnyTapMs > SETUP_EXIT_IDLE_MS) {
      exitSetupMode();
      renderTurn();
    } else {
      renderSetup();
    }
  }

  delay(5);
}
