#include <FastLED.h>
#include <Preferences.h>

// Targets ESP32-S3 (e.g. ESP32-S3-DevKitC-1). Piezo on ADC1 GPIO 1.
// Modes render on the external strip (GPIO 11) AND the onboard RGB pixel,
// so tap -> color cycling is testable before any strip is wired.
#define LED_PIN          11
#define NUM_LEDS         240   // buffer ceiling; the lit total is sideStarts[NUM_SIDES]
#define BRIGHTNESS       255   // FastLED's power cap does the real limiting (5 A DC fuse)
// Bench workaround while the strip has a single power feed: cap low enough
// that voltage droop stays invisible (confirmed: fade visible at 1.5 A,
// uniform at ~0.5 A). Restore to 4500 (the 5 A fuse ceiling) once the
// mid + end power-injection points are wired.
#define MAX_POWER_MA     700
#define LED_TYPE         WS2812B
#define COLOR_ORDER      GRB
#define NUM_SIDES        8

// Onboard RGB pixel, driven on both DevKitC-1 candidate pins (v1.0 = 48, v1.1 = 38)
#define ONBOARD_PIN_A    48
#define ONBOARD_PIN_B    38

const uint8_t PIEZO_PIN = 1;

// Adaptive tap detection: the piezo's resting level is averaged at boot, then
// tracked with a slow moving average. A tap fires when a reading jumps
// TAP_DELTA above that baseline. Spike readings never feed the average, so
// taps don't desensitize the sensor, while slow drift (temperature, mounting
// pressure, ambient vibration) is absorbed automatically.
const uint16_t TAP_DELTA = 1000;
const uint16_t DEBOUNCE_MS = 500;
const uint8_t NUM_MODES = 7;

// Per-side LED counts. Corner cuts eat 1-2 LEDs unpredictably, so the octagon's
// sides aren't uniform — this table is the source of truth for where each side
// starts. Calibrate over serial (0-7 select side, +/- adjust, p print, r reset,
// q quit); the result persists in NVS namespace "octagon", which turn_counter
// reads too, so a bench calibration carries over.
uint8_t sideLedCounts[NUM_SIDES] = {29, 28, 27, 27, 27, 28, 28, 27};  // calibrated 2026-07-21, 221 LEDs total
uint16_t sideStarts[NUM_SIDES + 1];  // prefix sums; sideStarts[NUM_SIDES] = lit total

CRGB leds[NUM_LEDS];
CRGB onboardA[1];
CRGB onboardB[1];
Preferences prefs;      // "taplight": last mode
Preferences sidePrefs;  // "octagon": calibrated side table
uint8_t currentMode = 0;
int8_t calibSide = -1;  // >= 0 while calibrating: strip shows side colors, taps don't change modes
bool calibBlink = true; // blink phase of the selected side (white <-> black)
uint32_t lastTapMs = 0;
uint32_t baselineAcc = 0;   // fixed-point moving average of the resting level, scaled by 64
uint8_t rainbowHue = 0;

uint16_t baseline() { return baselineAcc >> 6; }

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
}

void saveSideTable() {
  sidePrefs.putBytes("sides", sideLedCounts, NUM_SIDES);
}

void printSideTable() {
  Serial.print("uint8_t sideLedCounts[NUM_SIDES] = {");
  for (uint8_t s = 0; s < NUM_SIDES; s++) {
    Serial.printf("%u%s", sideLedCounts[s], s < NUM_SIDES - 1 ? ", " : "");
  }
  Serial.printf("};  // total %u LEDs\n", totalLeds());
}

// One distinct hue per side, so every physical corner should be a color flip.
// Hues advance by 96 (not 32) so neighbors sit far apart on the color wheel —
// 32-spacing put the sides in rainbow order and read as one smooth gradient.
// The side being calibrated blinks white instead.
void renderSideColors() {
  for (uint8_t s = 0; s < NUM_SIDES; s++) {
    CRGB c = (calibSide == s) ? (calibBlink ? CRGB(CRGB::White) : CRGB(CRGB::Black))
                              : CRGB(CHSV(s * 96, 255, 255));
    fill_solid(&leds[sideStarts[s]], sideLedCounts[s], c);
  }
  fill_solid(&leds[totalLeds()], NUM_LEDS - totalLeds(), CRGB::Black);
}

void renderMode(uint8_t mode) {
  uint16_t total = totalLeds();
  switch (mode) {
    case 0: fill_solid(leds, total, CRGB::White); break;  // connection test: every LED lit
    case 1: fill_solid(leds, total, CRGB(30, 100, 255)); break;
    case 2: fill_solid(leds, total, CRGB(60, 220, 80)); break;
    case 3: fill_solid(leds, total, CRGB(255, 200, 50)); break;
    case 4: renderSideColors(); break;
    case 5:
      for (uint16_t i = 0; i < total; i++) {
        leds[i] = CHSV((i * 256 / total), 255, 255);
      }
      break;
    case 6: fill_solid(leds, NUM_LEDS, CRGB::Black); break;
  }
  fill_solid(&leds[total], NUM_LEDS - total, CRGB::Black);
  // The onboard pixel mirrors the mode. Rainbow animates from loop() instead —
  // a single pixel can't show a gradient, so it cycles hue over time.
  onboardA[0] = onboardB[0] = (mode == 5) ? CRGB(CHSV(rainbowHue, 255, 255)) : leds[0];
  FastLED.show();
}

void renderCurrent() {
  if (calibSide >= 0) {
    renderSideColors();
    onboardA[0] = onboardB[0] = CHSV(calibSide * 96, 255, 255);
    FastLED.show();
  } else {
    renderMode(currentMode);
  }
}

void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c >= '0' && c < '0' + NUM_SIDES) {
      calibSide = c - '0';
      calibBlink = true;
      Serial.printf("Calibrating side %d (white): %u LEDs starting at %u. +/- adjusts, p prints, r resets, q quits.\n",
                    calibSide, sideLedCounts[calibSide], sideStarts[calibSide]);
      renderCurrent();
    } else if ((c == '+' || c == '=' || c == '-') && calibSide >= 0) {
      calibBlink = true;
      if (c == '-') {
        if (sideLedCounts[calibSide] > 1) sideLedCounts[calibSide]--;
      } else {
        if (sideLedCounts[calibSide] < 60 && totalLeds() < NUM_LEDS) sideLedCounts[calibSide]++;
      }
      rebuildSideStarts();
      saveSideTable();
      renderCurrent();
      Serial.printf("Side %d -> %u LEDs, strip total %u (saved)\n",
                    calibSide, sideLedCounts[calibSide], totalLeds());
    } else if (c == 'p') {
      printSideTable();
    } else if (c == 'r') {
      for (uint8_t s = 0; s < NUM_SIDES; s++) sideLedCounts[s] = 30;
      rebuildSideStarts();
      saveSideTable();
      renderCurrent();
      Serial.println("Side table reset to 8 x 30 (saved)");
    } else if (c == 'q') {
      calibSide = -1;
      renderCurrent();
      Serial.println("Calibration off");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  prefs.begin("taplight", false);
  currentMode = prefs.getUChar("mode", 0);
  if (currentMode >= NUM_MODES) currentMode = 0;

  sidePrefs.begin("octagon", false);
  loadSideTable();

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.addLeds<WS2812B, ONBOARD_PIN_A, GRB>(onboardA, 1);
  FastLED.addLeds<WS2812B, ONBOARD_PIN_B, GRB>(onboardB, 1);
  FastLED.setBrightness(BRIGHTNESS);
  // Full-strip white at 255 would pull ~14 A; scale any frame that would
  // exceed the 5 A DC-side fuse. Colored modes rarely hit this ceiling.
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_POWER_MA);

  pinMode(PIEZO_PIN, INPUT);

  // Seed the baseline with ~0.5 s of quiet readings before accepting taps.
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 100; i++) {
    sum += analogRead(PIEZO_PIN);
    delay(5);
  }
  baselineAcc = (sum / 100) << 6;
  Serial.printf("Piezo baseline: %u (tap fires at baseline + %u)\n", baseline(), TAP_DELTA);
  printSideTable();
  Serial.println("Calibration: 0-7 select side, +/- adjust, p print table, r reset, q quit");

  renderMode(currentMode);
}

void loop() {
  uint32_t now = millis();

  handleSerial();

  // Rainbow mode: keep the onboard pixel's hue moving so one LED reads as "rainbow"
  static uint32_t lastHueMs = 0;
  if (calibSide < 0 && currentMode == 5 && now - lastHueMs >= 20) {
    lastHueMs = now;
    rainbowHue++;
    onboardA[0] = onboardB[0] = CHSV(rainbowHue, 255, 255);
    FastLED.show();
  }

  uint16_t reading = analogRead(PIEZO_PIN);
  bool spike = reading > baseline() + TAP_DELTA;

  if (spike && now - lastTapMs > DEBOUNCE_MS) {
    lastTapMs = now;
    if (calibSide >= 0) {
      Serial.printf("Tap ignored during calibration (reading %u)\n", reading);
    } else {
      currentMode = (currentMode + 1) % NUM_MODES;
      prefs.putUChar("mode", currentMode);
      renderMode(currentMode);
      Serial.printf("Tap: reading %u, baseline %u (delta %u) -> mode %d\n",
                    reading, baseline(), reading - baseline(), currentMode);
    }
  } else if (!spike) {
    baselineAcc += reading - baseline();  // slow average, tau ~0.3 s at 5 ms/loop
  }

  delay(5);
}
