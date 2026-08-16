#pragma once
#include <Arduino.h>

// The phone UI, and the only thing the game knows about HTTP.
//
// The boundary: hardware facts (NUM_SIDES, PLAYER_COLORS) web_ui reads straight
// from octagon_core, because those describe the table. Game facts arrive only
// through this interface, so web_ui never sees joinOrder or prevRosterMask, and
// the game never sees a request object.

struct TableState {
  uint8_t mode;               // index into TableConfig::modeNames
  uint8_t brightnessPercent;  // 5..100, retained while off
  bool    lit;                // false = table dark, game state preserved
  int8_t  currentSide;        // -1 when the mode has no single current seat
  uint8_t rosterMask;         // bit s = seat s is in
  uint8_t readyMask;          // bit s = seat s is green (READY only)
  bool    inSetupMode;
  bool    locked;             // setup gesture refused while lit
};

struct TableConfig {
  const char* const* modeNames;
  uint8_t modeCount;
};

typedef void (*StateReader)(TableState &out);
typedef bool (*ModeSetter)(uint8_t mode);           // false = valid but refused
typedef bool (*BrightnessSetter)(uint8_t percent);  // false = valid but refused
typedef bool (*PowerSetter)(bool lit);              // always true today
typedef bool (*LockSetter)(bool locked);            // always true today

// Bundled rather than passed positionally: these are same-shaped function
// pointers, and swapping two of them compiles clean and fails at the table.
struct TableHooks {
  StateReader      read;
  ModeSetter       setMode;
  BrightnessSetter setBrightness;
  PowerSetter      setPower;
  LockSetter       setLock;
};

void webUiBegin(const TableConfig &cfg, const TableHooks &hooks);
void webUiHandle();   // call once per loop
void webUiEnd();      // on Wi-Fi loss, so the socket rebinds on reconnect
