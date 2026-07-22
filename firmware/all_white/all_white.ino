#include <FastLED.h>

// Dead-simple strip confirmation: every LED solid white, nothing else — no
// piezos, no logic. If this lights the strip, the LEDs + data + power are good
// and any "no lights" is upstream (firmware/piezo). If it DOESN'T light, the
// fault is the strip itself: GPIO 11 data wire, the 470 Ω, strip 5V/GND, or a
// broken common ground. Brightness is held to a modest, even level (well under
// the fuse) so a single power feed shows no droop.
#define LED_PIN       11
#define NUM_LEDS      240
#define MAX_POWER_MA  2000

CRGB leds[NUM_LEDS];

void setup() {
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(255);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_POWER_MA);
  fill_solid(leds, NUM_LEDS, CRGB::White);
  FastLED.show();
}

void loop() {
  // Re-send the frame each second so the strip recovers instantly if you
  // wiggle a connector while checking.
  delay(1000);
  FastLED.show();
}
