#include <FastLED.h>

// Connection smoke test: proves the toolchain, the USB link, and the board
// are alive before any wiring. No external parts needed.
//
// Twice a second it:
//   - prints a serial heartbeat (115200 baud) with chip details
//   - cycles the onboard RGB pixel red -> green -> blue -> off, driven on
//     BOTH candidate pins (GPIO 48 = DevKitC-1 v1.0 and most clones,
//     GPIO 38 = DevKitC-1 v1.1)
//
// If serial works but the LED stays dark: the sketch is running, so the
// connection is proven — look for an "RGB" solder jumper near the LED
// (some clone boards ship it open), or the LED is on a third pin.

#define PIN_A 48
#define PIN_B 38

CRGB ledA[1];
CRGB ledB[1];

const CRGB COLORS[] = { CRGB::Red, CRGB::Green, CRGB::Blue, CRGB::Black };
const char* NAMES[]  = { "RED", "GREEN", "BLUE", "off" };
uint8_t colorStep = 0;

void setup() {
  Serial.begin(115200);
  delay(500);

  FastLED.addLeds<WS2812B, PIN_A, GRB>(ledA, 1);
  FastLED.addLeds<WS2812B, PIN_B, GRB>(ledB, 1);
  FastLED.setBrightness(64);

  Serial.println();
  Serial.println("=== hello_board: connection smoke test ===");
  Serial.printf("Chip: %s rev %d, %d core(s), %lu MHz\n",
                ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getChipCores(), (unsigned long)ESP.getCpuFreqMHz());
  Serial.printf("Flash: %u KB   Free heap: %u KB\n",
                ESP.getFlashChipSize() / 1024, ESP.getFreeHeap() / 1024);
  uint64_t mac = ESP.getEfuseMac();
  Serial.printf("MAC: %04X%08X\n", (uint16_t)(mac >> 32), (uint32_t)mac);
  Serial.println("Onboard RGB driven on GPIO 48 and GPIO 38 (v1.0 / v1.1).");
}

void loop() {
  ledA[0] = COLORS[colorStep];
  ledB[0] = COLORS[colorStep];
  FastLED.show();
  Serial.printf("alive %6lu ms  RGB: %s\n", millis(), NAMES[colorStep]);
  colorStep = (colorStep + 1) % 4;
  delay(10000);
}
