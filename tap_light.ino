#include <FastLED.h>
#include <Preferences.h>

#define LED_PIN          5
#define NUM_LEDS         30
#define BRIGHTNESS       100
#define LED_TYPE         WS2812B
#define COLOR_ORDER      GRB

const uint8_t PIEZO_PIN = 32;
const uint16_t PIEZO_THRESHOLD = 400;
const uint16_t DEBOUNCE_MS = 250;
const uint8_t NUM_MODES = 6;

CRGB leds[NUM_LEDS];
Preferences prefs;
uint8_t currentMode = 0;
uint32_t lastTapMs = 0;

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
  FastLED.show();
}

void setup() {
  Serial.begin(115200);
  delay(200);

  prefs.begin("taplight", false);
  currentMode = prefs.getUChar("mode", 0);
  if (currentMode >= NUM_MODES) currentMode = 0;

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);

  pinMode(PIEZO_PIN, INPUT);

  renderMode(currentMode);
}

void loop() {
  uint32_t now = millis();
  uint16_t reading = analogRead(PIEZO_PIN);

  if (reading > PIEZO_THRESHOLD && now - lastTapMs > DEBOUNCE_MS) {
    lastTapMs = now;
    currentMode = (currentMode + 1) % NUM_MODES;
    prefs.putUChar("mode", currentMode);
    renderMode(currentMode);
    Serial.printf("Mode: %d (peak reading: %d)\n", currentMode, reading);
  }

  delay(5);
}
