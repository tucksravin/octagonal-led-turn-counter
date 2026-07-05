#include <FastLED.h>

// Minimal WS2812B test harness. No piezo, no Preferences, no taps needed —
// it re-sends data twice a second, so a strip that can light WILL light.
//
// Test A (software stack, zero wiring): drives the onboard RGB pixel on BOTH
//   candidate pins (GPIO 48 on DevKitC-1 v1.0, GPIO 38 on v1.1) simultaneously,
//   plus a serial heartbeat on the UART port to prove the sketch is running.
//
// Test B (strip + wiring, shifter bypassed): set STRIP_TEST to 1, wire
//   GPIO 11 -> 470R -> strip DIN directly, strip on 5V/GND as before.
//   3.3V data into a USB-powered strip (~4.7V VDD) is in-spec enough to work
//   at this length.

#define STRIP_TEST 1   // 0 = Test A (onboard pixel), 1 = Test B (external strip)

#if STRIP_TEST
#define LED_PIN   11
#define NUM_LEDS  30
CRGB leds[NUM_LEDS];
#else
CRGB leds48[1];
CRGB leds38[1];
#endif

void setup() {
  Serial.begin(115200);
#if STRIP_TEST
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
#else
  FastLED.addLeds<WS2812B, 48, GRB>(leds48, 1);
  FastLED.addLeds<WS2812B, 38, GRB>(leds38, 1);
#endif
  FastLED.setBrightness(64);
}

void blinkAll(const CRGB& c) {
#if STRIP_TEST
  fill_solid(leds, NUM_LEDS, c);
  if (c) leds[0] = CRGB::Green;  // first pixel green = data direction marker
#else
  leds48[0] = c;
  leds38[0] = c;
#endif
  FastLED.show();
}

void loop() {
  blinkAll(CRGB::Red);
  Serial.printf("alive %lu ms, pixels ON\n", millis());
  delay(500);
  blinkAll(CRGB::Black);
  delay(500);
}
