#pragma once
#include <Arduino.h>
#include <FastLED.h>

// Shared table hardware for the octagon: strip geometry, the calibrated
// per-side LED table, adaptive piezo tap detection, and tap dispatch.
//
// Everything here describes the physical table, not any one game — sketches
// layer the rules on top (turn_counter: setup mode + 4 modes; eight: plain
// clockwise passing). Nothing in here touches WiFi or OTA, which is what lets
// a sketch that skips them still fit the stock 1.25 MB app partition.
//
// Targets ESP32-S3 (e.g. ESP32-S3-DevKitC-1 N8R8). Piezos live on ADC1
// (GPIO 1-10) so they keep working while Wi-Fi/BLE are active.

#define LED_PIN          11
#define NUM_LEDS         240   // buffer ceiling; the lit total is totalLeds()
#define NUM_SIDES        8
// Brightness is runtime state, not a constant — see brightnessPercent() below.
#define BRIGHTNESS_DEFAULT_PCT 50  // 50% == raw 127, one step off the 128 this ran at for years
#define BRIGHTNESS_MIN_PCT     5   // floor, not zero: darkening the table entirely is the on/off
                                   // control's job, and a zeroed slider reads as broken
#define MAX_POWER_MA     1500  // FastLED auto-dims to hold LED draw here. Sized so the whole table
                               // can run through the ESP32-S3 dev board's own USB jack: the board's
                               // 5V path is good for ~1.5-2A, and 1500mA LEDs + ~250mA ESP ~= 1.75A.
                               // One-lit-side play (~0.6A) never hits the cap and stays full-bright;
                               // only the all-on moments (setup blink, READY, ready-flash) dim (~1/3).
                               // For full-brightness all-on, feed the strip 5V directly (not through
                               // the board) and raise this to ~2500 — see design doc 3.5.
#define LED_TYPE         WS2812B
#define COLOR_ORDER      GRB

extern CRGB leds[NUM_LEDS];

// All 8 piezos on ADC1. GPIO 3 is skipped because it's a JTAG strap pin.
extern const uint8_t PIEZO_PINS[NUM_SIDES];
extern const CRGB PLAYER_COLORS[NUM_SIDES];

// Which ADC pin serves each side. Defaults to PIEZO_PINS in order; a tap-guided
// calibration (runPiezoMapWizard) can permute it and store the result in NVS,
// so a crossed harness is fixed on the table instead of in the source. Always a
// permutation of PIEZO_PINS — an NVS table that isn't one is ignored.
extern uint8_t sidePiezoPin[NUM_SIDES];

// Per-side LED counts. Corner cuts eat 1-2 LEDs unpredictably, so the octagon's
// sides aren't uniform — this table is the source of truth for where each side
// starts. Defaults are overridden at boot by a table calibrated from the
// tap_light firmware (NVS namespace "octagon", serial commands 0-7/+/-/p).
extern uint8_t sideLedCounts[NUM_SIDES];
extern uint16_t sideStarts[NUM_SIDES + 1];  // prefix sums; sideStarts[NUM_SIDES] = lit total

// Fired for one committed tap. `whenMs` is when the tap was detected, which
// lags the callback by up to OPPOSITE_PAIR_WINDOW_MS when pairing is enabled.
typedef void (*TapHandler)(int8_t side, uint32_t whenMs);
typedef void (*PairHandler)();

// Loads the calibrated side table from NVS, brings up FastLED, then seeds the
// piezo baselines from ~0.5 s of quiet readings. Call Serial.begin() first —
// this reports the side table and the baselines it settled on.
void octagonBegin();

// `onOppositePair` handles a two-handed slap on diametrically-opposite sides.
// Pass nullptr if the sketch has no use for the gesture: taps then commit the
// instant they're detected instead of being held back to see whether the
// opposite side is about to spike.
void tapsBegin(TapHandler onTap, PairHandler onOppositePair);

// Scans the piezos and fires the callbacks. Call once per loop with millis().
void tapsPoll(uint32_t now);

// Drops a tap that's waiting to pair, so a gesture handler can consume the
// slap without the held tap landing afterwards.
void tapsReset();

// Blocking, LED-guided remap: lights each side in turn and records which piezo
// the operator taps, so nobody has to know which wire went where. Saves to NVS
// and re-seeds the baselines. Serial 'q', or 20 s of silence at any prompt,
// aborts with nothing written. Call from a sketch's serial handler; the caller
// must re-render afterwards.
void runPiezoMapWizard();

// A tap fires when a side reads this far above its own baseline. Exported so
// the piezo_test diagnostic can report against the same number the game uses.
extern const uint16_t TAP_DELTA;

uint32_t lastAnyTapMs();      // 0 until the first tap since boot; accepted fires only
uint32_t lastTapForSide(uint8_t i);  // 0 until side i's first tap since boot
bool sideMuted(uint8_t i);    // true while the tap guard has quarantined side i
                              // (chattering or stuck channel); self-clears after
                              // 5 s of quiet. See the guard block in the .cpp.
uint16_t totalLeds();
uint16_t baseline(uint8_t i);  // side i's resting ADC level, for bench diagnosis
bool isOppositeSide(int8_t a, int8_t b);
void printPiezoMap();          // current side->GPIO map as a paste-ready C array line

// LED brightness as a percentage, BRIGHTNESS_MIN_PCT..100. Persisted in NVS
// ("octagon"/"bri") so it survives a reboot and both sketches agree.
uint8_t brightnessPercent();

// Sets the target only — brightnessTick() eases the strip toward it, and the
// NVS write is deferred. Values outside the range are clamped, not rejected.
void setBrightnessPercent(uint8_t pct);

// Call once per loop with millis(). Eases the strip toward the target
// brightness, and writes the target to NVS once it has stopped moving, so
// dragging a slider costs one flash write rather than twenty.
void brightnessTick(uint32_t now);

void fillSide(uint8_t side, const CRGB &color);           // writes the buffer, no show()
void showOnlySide(int8_t side, const CRGB &color);        // that side lit, everything else dark
void renderOff();
void renderProgressBar(uint8_t percent, const CRGB &color);
