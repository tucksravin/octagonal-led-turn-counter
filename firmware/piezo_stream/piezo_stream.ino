#include <octagon_core.h>

// Piezo sensitivity recorder: streams all eight channels continuously so the
// host (scripts/record_piezos.py) can characterize noise floors, tap peaks
// and cross-talk instead of judging them from piezo_test's over-threshold
// reports.
//
// The loop scans the ADCs flat out (~1.3 kHz across all 8 channels) and every
// REPORT_MS emits one CSV line holding each channel's PEAK reading over that
// window. Peak-hold is the trick that makes 115200 baud enough: a raw 1 kHz
// stream wouldn't fit down the wire, and instantaneous samples at 100 Hz
// would alias right past a 2-3 ms piezo spike. Peaks survive decimation.
//
// Data rows look like "millis,p0,p1,...,p7" (raw 12-bit ADC counts). The
// boot banner and octagonBegin's table print as ordinary lines; the recorder
// files anything that isn't a data row away as a comment.
//
// LEDs stay dark on purpose — no point letting strip current pollute the
// analog rails while measuring the sensors' noise floor.

static const uint16_t REPORT_MS = 10;  // 100 rows/s ≈ 5.5 KB/s, half the wire's budget

uint16_t peak[NUM_SIDES] = {0};
uint32_t lastReportMs = 0;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("=== piezo_stream: 8-channel peak-hold CSV at 100 Hz ===");

  octagonBegin();
  renderOff();

  Serial.println("ms,p0,p1,p2,p3,p4,p5,p6,p7");
  lastReportMs = millis();
}

void loop() {
  for (uint8_t s = 0; s < NUM_SIDES; s++) {
    uint16_t v = analogRead(PIEZO_PINS[s]);
    if (v > peak[s]) peak[s] = v;
  }

  uint32_t now = millis();
  if (now - lastReportMs >= REPORT_MS) {
    lastReportMs = now;
    Serial.printf("%lu,%u,%u,%u,%u,%u,%u,%u,%u\n", (unsigned long)now,
                  peak[0], peak[1], peak[2], peak[3],
                  peak[4], peak[5], peak[6], peak[7]);
    for (uint8_t s = 0; s < NUM_SIDES; s++) peak[s] = 0;
  }
}
