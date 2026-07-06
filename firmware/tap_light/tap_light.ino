#include <FastLED.h>
#include <Preferences.h>

// Targets ESP32-S3 (e.g. ESP32-S3-DevKitC-1). Piezo on ADC1 GPIO 1.
// Modes render on the external strip (GPIO 11) AND the onboard RGB pixel,
// so tap -> color cycling is testable before any strip is wired.
#define LED_PIN          11
#define NUM_LEDS         30
#define BRIGHTNESS       100
#define LED_TYPE         WS2812B
#define COLOR_ORDER      GRB

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
const uint8_t NUM_MODES = 6;

CRGB leds[NUM_LEDS];
CRGB onboardA[1];
CRGB onboardB[1];
Preferences prefs;
uint8_t currentMode = 0;
uint32_t lastTapMs = 0;
uint32_t baselineAcc = 0;   // fixed-point moving average of the resting level, scaled by 64
uint8_t rainbowHue = 0;

uint16_t baseline() { return baselineAcc >> 6; }

void renderMode(uint8_t mode) {
  switch (mode) {
    case 0: fill_solid(leds, NUM_LEDS, CRGB(255, 80, 30)); break;
    case 1: fill_solid(leds, NUM_LEDS, CRGB(30, 100, 255)); break;
    case 2: fill_solid(leds, NUM_LEDS, CRGB(60, 220, 80)); break;
    case 3: fill_solid(leds, NUM_LEDS, CRGB(255, 200, 50)); break;
    case 4:
      for (uint16_t i = 0; i < NUM_LEDS; i++) {
        leds[i] = CHSV((i * 256 / NUM_LEDS), 255, 255);
      }
      break;
    case 5: fill_solid(leds, NUM_LEDS, CRGB::Black); break;
  }
  // The onboard pixel mirrors the mode. Rainbow animates from loop() instead —
  // a single pixel can't show a gradient, so it cycles hue over time.
  onboardA[0] = onboardB[0] = (mode == 4) ? CRGB(CHSV(rainbowHue, 255, 255)) : leds[0];
  FastLED.show();
}

void setup() {
  Serial.begin(115200);
  delay(200);

  prefs.begin("taplight", false);
  currentMode = prefs.getUChar("mode", 0);
  if (currentMode >= NUM_MODES) currentMode = 0;

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.addLeds<WS2812B, ONBOARD_PIN_A, GRB>(onboardA, 1);
  FastLED.addLeds<WS2812B, ONBOARD_PIN_B, GRB>(onboardB, 1);
  FastLED.setBrightness(BRIGHTNESS);

  pinMode(PIEZO_PIN, INPUT);

  // Seed the baseline with ~0.5 s of quiet readings before accepting taps.
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 100; i++) {
    sum += analogRead(PIEZO_PIN);
    delay(5);
  }
  baselineAcc = (sum / 100) << 6;
  Serial.printf("Piezo baseline: %u (tap fires at baseline + %u)\n", baseline(), TAP_DELTA);

  renderMode(currentMode);
}

void loop() {
  uint32_t now = millis();

  // Rainbow mode: keep the onboard pixel's hue moving so one LED reads as "rainbow"
  static uint32_t lastHueMs = 0;
  if (currentMode == 4 && now - lastHueMs >= 20) {
    lastHueMs = now;
    rainbowHue++;
    onboardA[0] = onboardB[0] = CHSV(rainbowHue, 255, 255);
    FastLED.show();
  }

  uint16_t reading = analogRead(PIEZO_PIN);
  bool spike = reading > baseline() + TAP_DELTA;

  if (spike && now - lastTapMs > DEBOUNCE_MS) {
    lastTapMs = now;
    currentMode = (currentMode + 1) % NUM_MODES;
    prefs.putUChar("mode", currentMode);
    renderMode(currentMode);
    Serial.printf("Tap: reading %u, baseline %u (delta %u) -> mode %d\n",
                  reading, baseline(), reading - baseline(), currentMode);
  } else if (!spike) {
    baselineAcc += reading - baseline();  // slow average, tau ~0.3 s at 5 ms/loop
  }

  delay(5);
}
