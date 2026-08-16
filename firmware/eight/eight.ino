#include <octagon_core.h>

// The turn counter with the lid off: all eight seats always in, no setup mode,
// no game modes, no roster, no WiFi. One side is lit in its own color; tapping
// that side passes the turn clockwise. Taps anywhere else do nothing.
//
// Everything about the table itself — the calibrated side table, piezo
// baselines, tap detection — comes from the octagon_core library, so this and
// turn_counter always agree on the hardware. Skipping WiFi/OTA keeps the
// binary inside the stock 1.25 MB app partition, so `make flash-eight` doesn't
// need turn_counter's min_spiffs scheme.
//
// State lives only in RAM: reset returns to side 0.

int8_t currentSide = 0;

void showTurn() {
  showOnlySide(currentSide, PLAYER_COLORS[currentSide]);
}

// nullptr pair handler in setup() means taps commit immediately, so this
// fires the moment the piezo trips.
void onTap(int8_t side, uint32_t whenMs) {
  (void)whenMs;
  if (side != currentSide) {
    Serial.printf("Tap on side %d ignored - not the current seat\n", side);
    return;
  }
  currentSide = (currentSide + 1) % NUM_SIDES;
  Serial.printf("Turn passed to side %d\n", currentSide);
  showTurn();
}

// Bench commands over USB serial: m remaps the piezos, p prints the map.
void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'm') {
      runPiezoMapWizard();
      showTurn();
    } else if (c == 'p') {
      printPiezoMap();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("=== eight: all seats in, clockwise passing ===");

  octagonBegin();
  tapsBegin(onTap, nullptr);  // no two-handed power toggle in this build
  showTurn();
}

void loop() {
  handleSerial();
  uint32_t now = millis();
  brightnessTick(now);
  tapsPoll(now);
  delay(5);
}
