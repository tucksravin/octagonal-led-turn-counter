#include <octagon_core.h>

// Piezo bring-up diagnostic: reads all eight ADCs directly instead of going
// through the game's readPiezos(), so nothing is filtered out. Every side that
// clears the trigger in a hit window gets reported with its own numbers — which
// is what shows you cross-talk, a weak glue joint, or a dead channel.
//
// Two deliberate differences from the game firmware:
//
//   - TRIGGER_DELTA is far below the game's TAP_DELTA, so weak hits still show
//     up. Each reported side is marked "GAME" if it also cleared TAP_DELTA,
//     i.e. whether the real firmware would have counted that tap.
//   - Baselines are seeded once by octagonBegin() and then left frozen (the
//     slow moving average lives inside readPiezos, which this never calls). A
//     fixed reference keeps the deltas comparable across a session; the idle
//     dump prints the live readings next to it so drift is still visible.
//
// Reading a side rail-to-rail (~4095) with a steady baseline means that pin
// lost its ground return — a floating ADC input, not a threshold problem.
// Re-seed the baselines by resetting the board (`make ping`).

static const uint16_t TRIGGER_DELTA   = 300;   // report threshold; TAP_DELTA is the game's
static const uint16_t HIT_WINDOW_MS   = 120;   // collect peaks this long after the first trip
static const uint16_t LED_HOLD_MS     = 400;   // how long the sides that fired stay lit
static const uint32_t IDLE_DUMP_MS    = 3000;  // baseline/live dump cadence while quiet

uint16_t peakDelta[NUM_SIDES] = {0};
bool     inHit = false;
uint32_t hitStartMs = 0;
uint32_t hitCount = 0;
uint32_t ledsOffAtMs = 0;
uint32_t lastDumpMs = 0;

void reportHit(uint32_t now) {
  hitCount++;

  uint8_t loudest = 0;
  for (uint8_t s = 1; s < NUM_SIDES; s++) {
    if (peakDelta[s] > peakDelta[loudest]) loudest = s;
  }

  Serial.printf("\n--- hit #%lu at %lu ms ---\n", (unsigned long)hitCount, (unsigned long)now);
  FastLED.clear();
  for (uint8_t s = 0; s < NUM_SIDES; s++) {
    if (peakDelta[s] < TRIGGER_DELTA) continue;
    Serial.printf("  side %u  baseline %4u  peak %4u  delta %4u  %-4s%s\n",
                  s, baseline(s), baseline(s) + peakDelta[s], peakDelta[s],
                  peakDelta[s] >= TAP_DELTA ? "GAME" : "weak",
                  s == loudest ? "  <- loudest" : "");
    fillSide(s, PLAYER_COLORS[s]);
  }
  FastLED.show();
  ledsOffAtMs = now + LED_HOLD_MS;
}

void dumpIdle(const uint16_t *live) {
  Serial.print("idle  baseline/live:");
  for (uint8_t s = 0; s < NUM_SIDES; s++) {
    Serial.printf("  %u:%u/%u", s, baseline(s), live[s]);
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("=== piezo_test: all eight channels, raw ===");

  octagonBegin();

  Serial.printf("Reporting any side over baseline + %u; the game needs + %u.\n",
                TRIGGER_DELTA, TAP_DELTA);
  Serial.println("Tap each side in turn — every channel that trips is listed with its delta.");
  renderOff();
}

void loop() {
  uint32_t now = millis();
  uint16_t live[NUM_SIDES];
  bool tripped = false;

  for (uint8_t s = 0; s < NUM_SIDES; s++) {
    live[s] = analogRead(PIEZO_PINS[s]);
    uint16_t delta = live[s] > baseline(s) ? live[s] - baseline(s) : 0;
    if (delta > peakDelta[s]) peakDelta[s] = delta;
    if (delta >= TRIGGER_DELTA) tripped = true;
  }

  if (inHit) {
    if (now - hitStartMs >= HIT_WINDOW_MS) {
      reportHit(now);
      inHit = false;
      for (uint8_t s = 0; s < NUM_SIDES; s++) peakDelta[s] = 0;
    }
  } else if (tripped) {
    inHit = true;
    hitStartMs = now;  // peaks from this scan carry into the window
  } else {
    for (uint8_t s = 0; s < NUM_SIDES; s++) peakDelta[s] = 0;

    if (now - lastDumpMs >= IDLE_DUMP_MS) {
      lastDumpMs = now;
      dumpIdle(live);
    }
  }

  if (ledsOffAtMs != 0 && now >= ledsOffAtMs) {
    ledsOffAtMs = 0;
    renderOff();
  }

  delay(2);  // faster than the game's 5 ms — piezo ring-down is brief
}
