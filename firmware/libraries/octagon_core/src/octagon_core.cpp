#include "octagon_core.h"
#include <Preferences.h>

CRGB leds[NUM_LEDS];

const uint8_t PIEZO_PINS[NUM_SIDES] = {1, 2, 4, 5, 6, 7, 8, 9};

// Filled by loadPiezoMap() at boot — PIEZO_PINS order, or the calibrated
// permutation from NVS. Not usable before octagonBegin().
uint8_t sidePiezoPin[NUM_SIDES] = {0};

const CRGB PLAYER_COLORS[NUM_SIDES] = {
  CRGB(255, 40, 40),
  CRGB(40, 120, 255),
  CRGB(60, 220, 80),
  CRGB(255, 200, 30),
  CRGB(220, 60, 220),
  CRGB(40, 230, 230),
  CRGB(255, 130, 30),
  CRGB(180, 180, 220)
};

uint8_t sideLedCounts[NUM_SIDES] = {29, 28, 27, 27, 27, 28, 28, 27};  // calibrated 2026-07-21, 221 LEDs total
uint16_t sideStarts[NUM_SIDES + 1];

// Adaptive tap detection (same approach as tap_light.ino): each side's resting
// ADC level is averaged at boot, then tracked with a slow moving average. A tap
// fires when a reading jumps TAP_DELTA above that side's own baseline. Spike
// readings never feed the average, so taps don't desensitize a side, while slow
// drift (temperature, glue aging, ambient vibration) is absorbed automatically.
// 720 sits at the top of the gap between the soft-tap cluster (<490) and the
// weakest real tap (757) in the 2026-07-28 bench recordings (data/piezo/),
// with ~12x headroom over the worst idle noise seen (59).
const uint16_t TAP_DELTA                      = 720;
static const uint16_t DEBOUNCE_MS             = 250;
static const uint16_t OPPOSITE_PAIR_WINDOW_MS = 150;

// Tap guard: keeps one broken channel from taking the table down (spec:
// docs/superpowers/specs/2026-08-16-tap-guard-design.md). Three fault modes,
// three mechanisms — a sustained ring is stopped by hysteresis re-arm, a
// pinned-at-rail input by the stuck timeout, oscillating mains pickup by the
// rate quarantine. Values chosen so healthy play never touches any of them:
// the setup gesture's taps land >= ~450 ms apart, no real ring survives 2 s
// above half-threshold, and 8 consecutive sub-400 ms fires is a machine, not
// a hand.
static const uint16_t CHATTER_GAP_MS          = 400;    // a fire this soon after the same side's
                                                        // previous fire counts toward its streak
static const uint8_t  CHATTER_STREAK          = 8;      // streak length that mutes the side
static const uint16_t STUCK_MUTE_MS           = 2000;   // disarmed this long without re-arming = pinned
static const uint16_t UNMUTE_QUIET_MS         = 5000;   // continuous quiet that restores a muted side
static const uint16_t MAP_PROMPT_TIMEOUT_MS   = 20000;  // silence at a prompt aborts the whole remap
static const uint16_t MAP_SETTLE_MS           = 400;    // ring-down gap so one tap can't answer two prompts
static const uint16_t BRIGHTNESS_SETTLE_MS    = 2000;   // quiet time before a slider change hits flash
static const uint16_t BRIGHTNESS_FRAME_MS     = 16;     // ~60 Hz. A show() is ~7 ms of blocking bit-bang
                                                        // for 221 pixels, so this can't run every loop pass
static const uint8_t  BRIGHTNESS_FADE_ALPHA   = 40;     // of 255 per frame. Settle time scales with distance:
                                                        // ~130 ms for a 1% nudge, ~540 ms for a full 5->100% sweep

static Preferences sidePrefs;

static uint32_t baselineAcc[NUM_SIDES] = {0};  // fixed-point moving averages of resting levels, scaled by 64
static uint32_t lastTapPerSide[NUM_SIDES] = {0};
static uint32_t lastTapMs = 0;

// Tap-guard state, all RAM-only — a power cycle retries the hardware fresh.
static bool     armed[NUM_SIDES]      = {true, true, true, true, true, true, true, true};
static uint8_t  fireStreak[NUM_SIDES] = {0};
static bool     muted[NUM_SIDES]      = {false};
static uint32_t lastLoudMs[NUM_SIDES] = {0};   // last scan at/above the re-arm level

static TapHandler  tapCb  = nullptr;
static PairHandler pairCb = nullptr;
static int8_t   pendingSide = -1;
static uint32_t pendingMs = 0;

uint16_t baseline(uint8_t i) { return baselineAcc[i] >> 6; }

uint32_t lastAnyTapMs() { return lastTapMs; }

uint32_t lastTapForSide(uint8_t i) { return (i < NUM_SIDES) ? lastTapPerSide[i] : 0; }

bool sideMuted(uint8_t i) { return (i < NUM_SIDES) ? muted[i] : false; }

uint16_t totalLeds() { return sideStarts[NUM_SIDES]; }

static void rebuildSideStarts() {
  sideStarts[0] = 0;
  for (uint8_t s = 0; s < NUM_SIDES; s++) {
    sideStarts[s + 1] = sideStarts[s] + sideLedCounts[s];
  }
}

static bool validSideTable(const uint8_t *t) {
  uint16_t total = 0;
  for (uint8_t s = 0; s < NUM_SIDES; s++) {
    if (t[s] < 1 || t[s] > 60) return false;
    total += t[s];
  }
  return total <= NUM_LEDS;
}

static void loadSideTable() {
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

// Valid only if the stored table is a true permutation of PIEZO_PINS: every
// entry a pin we actually read, and no pin serving two sides. This is what makes
// an NVS table written by a different firmware safe to ignore.
static bool validPiezoMap(const uint8_t *m) {
  uint8_t seen = 0;
  for (uint8_t s = 0; s < NUM_SIDES; s++) {
    uint8_t idx = 0xFF;
    for (uint8_t i = 0; i < NUM_SIDES; i++) {
      if (PIEZO_PINS[i] == m[s]) { idx = i; break; }
    }
    if (idx == 0xFF || (seen & (1 << idx))) return false;
    seen |= (1 << idx);
  }
  return true;
}

void printPiezoMap() {
  Serial.print("uint8_t sidePiezoPin[NUM_SIDES] = {");
  for (uint8_t s = 0; s < NUM_SIDES; s++) {
    Serial.printf("%u%s", sidePiezoPin[s], s < NUM_SIDES - 1 ? ", " : "");
  }
  Serial.println("};");
}

static void loadPiezoMap() {
  memcpy(sidePiezoPin, PIEZO_PINS, NUM_SIDES);
  uint8_t stored[NUM_SIDES];
  if (sidePrefs.getBytes("pmap", stored, NUM_SIDES) == NUM_SIDES && validPiezoMap(stored)) {
    memcpy(sidePiezoPin, stored, NUM_SIDES);
    Serial.println("Loaded calibrated piezo map from NVS");
  }
  printPiezoMap();
}

bool isOppositeSide(int8_t a, int8_t b) {
  if (a < 0 || b < 0) return false;
  return ((a + NUM_SIDES / 2) % NUM_SIDES) == b;
}

void fillSide(uint8_t side, const CRGB &color) {
  if (side >= NUM_SIDES) return;
  fill_solid(&leds[sideStarts[side]], sideLedCounts[side], color);
}

void showOnlySide(int8_t side, const CRGB &color) {
  FastLED.clear();
  if (side >= 0 && side < NUM_SIDES) fillSide(side, color);
  FastLED.show();
}

void renderOff() {
  FastLED.clear();
  FastLED.show();
}

void renderProgressBar(uint8_t percent, const CRGB &color) {
  FastLED.clear();
  uint16_t lit = (uint32_t)totalLeds() * percent / 100;
  for (uint16_t i = 0; i < lit; i++) {
    leds[i] = color;
  }
  FastLED.show();
}

// Commit a fire on side i: refractory bookkeeping plus chatter accounting.
// Returns false when the fire is swallowed (debounce, or the fire that crossed
// the quarantine line). lastTapMs — the clock the games' idle/setup timers key
// off — moves only on ACCEPTED fires, so a faulted side can no longer hold
// setup open by refreshing it.
static bool commitFire(uint8_t i, uint32_t now) {
  uint32_t gap = now - lastTapPerSide[i];
  if (gap <= DEBOUNCE_MS) return false;
  armed[i] = false;                        // re-arms on a scan below baseline + TAP_DELTA/2
  fireStreak[i] = (lastTapPerSide[i] != 0 && gap < CHATTER_GAP_MS) ? fireStreak[i] + 1 : 1;
  lastTapPerSide[i] = now;
  if (fireStreak[i] >= CHATTER_STREAK) {
    muted[i] = true;
    Serial.printf("Piezo side %u MUTED - %u fires at machine pace (chatter). "
                  "Ignoring it until it stays quiet %u ms.\n",
                  i, fireStreak[i], UNMUTE_QUIET_MS);
    return false;
  }
  lastTapMs = now;
  return true;
}

// Returns a bitmask of accepted hits in this scan. At most two bits set:
// the side with the biggest jump above its own baseline (cross-talk filter —
// adjacent ghosts lose to the real hit) and, if its diametrically-opposite
// side also spiked this scan, that one too (two-handed slap detected in a
// single scan, before debounce can lock it out). Cross-scan two-handed
// slaps still work via pendingSide in tapsPoll().
//
// The tap guard threads through this: muted sides are invisible (they can
// neither fire nor be the scan's winner, so a faulted channel can't starve
// the healthy seven), while a HOT side that has fired and not yet re-armed
// still wins the scan and suppresses it — that shadow is the existing ghost
// filter, and the bounded pre-mute window where a fault can eat real taps is
// the price of keeping it.
static uint8_t readPiezos(uint32_t now) {
  uint8_t hotMask = 0;
  uint8_t maxIdx = 0xFF;
  uint16_t maxDelta = 0;

  for (uint8_t i = 0; i < NUM_SIDES; i++) {
    uint16_t reading = analogRead(sidePiezoPin[i]);
    uint16_t base = baseline(i);
    bool hot = reading > base + TAP_DELTA;

    if (reading >= base + TAP_DELTA / 2) {
      lastLoudMs[i] = now;                 // loud: blocks re-arm and the unmute clock
    } else {
      armed[i] = true;                     // dipped below hysteresis: eligible again
    }
    if (!hot) {
      baselineAcc[i] += reading - base;    // slow average, tau ~0.3 s at 5 ms/loop
    }

    if (muted[i]) {
      if (now - lastLoudMs[i] >= UNMUTE_QUIET_MS) {
        muted[i] = false;
        fireStreak[i] = 0;
        Serial.printf("Piezo side %u unmuted - quiet for %u ms\n", i, UNMUTE_QUIET_MS);
      }
      continue;                            // invisible: no fire, no shadow
    }

    if (!armed[i] && now - lastTapPerSide[i] > STUCK_MUTE_MS) {
      // Fired and never re-armed: no real ring survives this long above
      // half-threshold. A pinned input would otherwise shadow the whole
      // table forever without ever firing again — a silent outage.
      muted[i] = true;
      fireStreak[i] = 0;
      Serial.printf("Piezo side %u MUTED - stuck above re-arm level for %u ms "
                    "(floating input?). Ignoring it until it stays quiet %u ms.\n",
                    i, STUCK_MUTE_MS, UNMUTE_QUIET_MS);
      continue;
    }

    if (hot) {
      hotMask |= (1 << i);
      uint16_t delta = reading - base;
      if (delta > maxDelta) {
        maxDelta = delta;
        maxIdx = i;
      }
    }
  }

  if (maxIdx == 0xFF) return 0;
  if (!armed[maxIdx]) return 0;            // ghost era: the loudest side already
                                           // fired and hasn't gone quiet yet
  if (!commitFire(maxIdx, now)) return 0;

  uint8_t resultMask = (1 << maxIdx);
  uint8_t oppIdx = (maxIdx + NUM_SIDES / 2) % NUM_SIDES;
  if ((hotMask & (1 << oppIdx)) && armed[oppIdx] && commitFire(oppIdx, now)) {
    resultMask |= (1 << oppIdx);
  }

  return resultMask;
}

void tapsBegin(TapHandler onTap, PairHandler onOppositePair) {
  tapCb = onTap;
  pairCb = onOppositePair;
  pendingSide = -1;
}

void tapsReset() {
  pendingSide = -1;
}

// With a pair handler installed, a tap is held back one window so the opposite
// side gets a chance to spike and claim it as a two-handed slap; a second tap
// elsewhere commits the held one early. Without a handler there's nothing to
// wait for, so taps commit on detection.
void tapsPoll(uint32_t now) {
  uint8_t hits = readPiezos(now);

  for (uint8_t i = 0; i < NUM_SIDES; i++) {
    if (!(hits & (1 << i))) continue;

    if (!pairCb) {
      if (tapCb) tapCb(i, now);
      continue;
    }

    if (pendingSide >= 0 && isOppositeSide(pendingSide, i)) {
      pendingSide = -1;
      pairCb();
      continue;
    }

    if (pendingSide >= 0 && tapCb) {
      int8_t held = pendingSide;
      uint32_t heldMs = pendingMs;
      pendingSide = -1;
      tapCb(held, heldMs);
    }

    pendingSide = i;
    pendingMs = now;
  }

  if (pairCb && pendingSide >= 0 && now - pendingMs >= OPPOSITE_PAIR_WINDOW_MS) {
    int8_t held = pendingSide;
    uint32_t heldMs = pendingMs;
    pendingSide = -1;
    if (tapCb) tapCb(held, heldMs);
  }
}

static uint8_t  briPct       = BRIGHTNESS_DEFAULT_PCT;  // target
static uint16_t briShown256  = 0;                       // what's actually on the strip, 8.8 fixed point
static uint32_t briFrameMs   = 0;
static bool     briDirty     = false;
static uint32_t briChangedMs = 0;

uint8_t brightnessPercent() { return briPct; }

// FastLED wants 0-255. The floor is applied here too, so no code path can
// produce a dark table by way of the brightness control.
static uint8_t briRaw(uint8_t pct) {
  uint16_t raw = (uint16_t)pct * 255 / 100;
  uint16_t floorRaw = (uint16_t)BRIGHTNESS_MIN_PCT * 255 / 100;
  if (raw < floorRaw) raw = floorRaw;
  if (raw > 255) raw = 255;
  return (uint8_t)raw;
}

void setBrightnessPercent(uint8_t pct) {
  if (pct < BRIGHTNESS_MIN_PCT) pct = BRIGHTNESS_MIN_PCT;
  if (pct > 100) pct = 100;
  if (pct == briPct) return;
  briPct = pct;                  // target only; brightnessTick() eases toward it
  briDirty = true;               // and the flash write waits for the drag to stop
  briChangedMs = millis();
}

// Eases the strip toward the target brightness, then persists the target once it
// has stopped moving.
//
// The easing is the lerp a web animation would use — current += (target -
// current) * alpha — rather than a fixed-duration tween, because the phone
// slider retargets up to four times a second while dragging and a tween would
// have to restart on each one, which stutters. Smoothing just follows, and eases
// out for free.
//
// Two constraints shape the rest. FastLED.setBrightness() does nothing on its
// own — it only takes effect on the next show() — and in steady play nothing
// redraws, so this has to issue the show() itself. But a show() blocks for ~7 ms
// bit-banging 221 pixels, which is longer than the loop's own 5 ms cadence, so
// it's gated to one step per frame and runs only while a fade is in flight.
void brightnessTick(uint32_t now) {
  if (now - briFrameMs >= BRIGHTNESS_FRAME_MS) {
    briFrameMs = now;
    uint16_t target256 = (uint16_t)briRaw(briPct) << 8;
    if (briShown256 != target256) {
      int32_t delta = (int32_t)target256 - (int32_t)briShown256;
      if (delta > -256 && delta < 256) {
        // Under one raw unit: the strip cannot show the difference, so land now
        // instead of crawling the exponential tail. Without this a 1% nudge kept
        // firing show() for ~600 ms after it was already visually finished.
        briShown256 = target256;
      } else {
        // Always undershoots (alpha < 1), so it can't overshoot the target.
        briShown256 = (uint16_t)((int32_t)briShown256 + delta * BRIGHTNESS_FADE_ALPHA / 255);
      }
      FastLED.setBrightness((uint8_t)(briShown256 >> 8));
      FastLED.show();   // re-sends the frame already in leds[] at the new scale
    }
  }

  if (!briDirty || now - briChangedMs < BRIGHTNESS_SETTLE_MS) return;
  briDirty = false;
  sidePrefs.putUChar("bri", briPct);
  Serial.printf("LED brightness saved: %u%%\n", briPct);
}

static void loadBrightness() {
  uint8_t stored = sidePrefs.getUChar("bri", BRIGHTNESS_DEFAULT_PCT);
  briPct = (stored >= BRIGHTNESS_MIN_PCT && stored <= 100) ? stored : BRIGHTNESS_DEFAULT_PCT;
  Serial.printf("LED brightness: %u%%\n", briPct);
}

// Seeds every baseline from ~0.5 s of quiet readings before taps are accepted.
// Takes the pin array so the same buffer can be seeded side-indexed
// (sidePiezoPin, for play) or channel-indexed (PIEZO_PINS, for the remap wizard).
static void seedBaselines(const uint8_t *pins) {
  uint32_t sums[NUM_SIDES] = {0};
  for (uint8_t s = 0; s < 100; s++) {
    for (uint8_t i = 0; i < NUM_SIDES; i++) {
      sums[i] += analogRead(pins[i]);
    }
    delay(5);
  }
  Serial.print("Piezo baselines:");
  for (uint8_t i = 0; i < NUM_SIDES; i++) {
    baselineAcc[i] = (sums[i] / 100) << 6;
    Serial.printf(" %u", baseline(i));
  }
  Serial.printf(" (tap fires at baseline + %u)\n", TAP_DELTA);
}

// Lights one side at a time and records which physical piezo answers. Reads
// PIEZO_PINS directly, bypassing the map it is replacing — a crossed harness
// can't confuse the wizard that fixes it. Baselines are channel-indexed for the
// duration and re-seeded side-indexed on the way out.
void runPiezoMapWizard() {
  uint8_t built[NUM_SIDES];
  uint8_t claimed = 0;  // bitmask of hardware channels already assigned

  Serial.println("MAP START - tap each lit side; q aborts");
  seedBaselines(PIEZO_PINS);

  for (uint8_t s = 0; s < NUM_SIDES; ) {
    showOnlySide(s, CRGB(CRGB::White));
    Serial.printf("MAP side %u - tap the lit seat\n", s);

    int8_t hit = -1;
    uint32_t promptedMs = millis();
    while (hit < 0) {
      if (millis() - promptedMs > MAP_PROMPT_TIMEOUT_MS) {
        Serial.println("MAP TIMEOUT - nothing saved");
        renderOff();
        seedBaselines(sidePiezoPin);
        return;
      }
      if (Serial.available() && Serial.read() == 'q') {
        Serial.println("MAP ABORT - nothing saved");
        renderOff();
        seedBaselines(sidePiezoPin);
        return;
      }
      // Biggest jump above its own baseline wins, so an adjacent side's
      // cross-talk ghost loses to the seat actually struck.
      uint16_t best = 0;
      for (uint8_t c = 0; c < NUM_SIDES; c++) {
        uint16_t reading = analogRead(PIEZO_PINS[c]);
        if (reading > baseline(c) + TAP_DELTA) {
          uint16_t delta = reading - baseline(c);
          if (delta > best) { best = delta; hit = c; }
        } else {
          baselineAcc[c] += reading - baseline(c);
        }
      }
      delay(5);
    }

    if (claimed & (1 << hit)) {
      uint8_t owner = 0;
      for (uint8_t p = 0; p < s; p++) {
        if (built[p] == PIEZO_PINS[hit]) { owner = p; break; }
      }
      Serial.printf("MAP DUP - GPIO %u already mapped to side %u, retry\n", PIEZO_PINS[hit], owner);
      showOnlySide(s, CRGB(200, 0, 0));
      delay(MAP_SETTLE_MS);
      continue;  // same side, prompt again
    }

    built[s] = PIEZO_PINS[hit];
    claimed |= (1 << hit);
    Serial.printf("MAP side %u = GPIO %u\n", s, PIEZO_PINS[hit]);
    showOnlySide(s, CRGB(0, 200, 0));
    delay(MAP_SETTLE_MS);
    s++;
  }

  memcpy(sidePiezoPin, built, NUM_SIDES);
  sidePrefs.putBytes("pmap", sidePiezoPin, NUM_SIDES);

  fill_solid(leds, totalLeds(), CRGB(0, 200, 0));
  FastLED.show();
  delay(600);
  renderOff();

  seedBaselines(sidePiezoPin);
  Serial.println("MAP DONE - saved to NVS");
  printPiezoMap();
}

void octagonBegin() {
  sidePrefs.begin("octagon", false);
  loadSideTable();
  loadPiezoMap();

  loadBrightness();
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  briShown256 = (uint16_t)briRaw(briPct) << 8;   // start already at the target: boot is
  FastLED.setBrightness(briRaw(briPct));         // not a transition, so no fade-in
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_POWER_MA);  // runs off the board's USB; dims all-on peaks

  // Every hardware pin is configured regardless of the map — the remap wizard
  // reads them all directly.
  for (uint8_t i = 0; i < NUM_SIDES; i++) {
    pinMode(PIEZO_PINS[i], INPUT);
  }

  seedBaselines(sidePiezoPin);
}
