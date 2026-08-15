# Phone control — mode, brightness and off

**Date**: 2026-08-15
**Status**: approved

## Goal

Let anyone at the table change the game mode, the LED brightness, and whether
the table is lit at all, from their phone — and see what the table is currently
doing, without installing anything.

Today mode is reachable only through the 4-tap setup gesture, which also resets
the roster — there is no way to switch CW→CCW without re-seating everybody.
Brightness is a compile-time constant, so changing it means a reflash. And there
is no way to darken the table short of unplugging it.

Scope is deliberately mode, brightness and on/off. The roster stays tap-driven.

**Off is a separate control from brightness, not the bottom of the slider.**
Brightness keeps its value while off, so turning back on restores what you had
rather than leaving you staring at a dark table and a zeroed slider trying to
work out whether it's broken.

## Non-goals

- No roster/seat editing from the phone.
- No auth. The table is on a trusted LAN, same posture as OTA. Anyone who can
  reach it can change mode and brightness — at a game table that is the point.
  Nothing destructive is exposed: no OTA trigger, no NVS wipe, no remap.
- No WebSocket. A 2 s poll is enough for a turn counter.
- `eight` stays radio-free, as with OTA. It gains the runtime brightness (which
  needs no radio) but no web server.

## Architecture

### Boundary

`turn_counter.ino` is ~570 lines. The web layer would add a server plus an
embedded page, so it goes in its own translation unit in the sketch folder —
`web_ui.h` / `web_ui.cpp`, which arduino-cli compiles automatically alongside
the `.ino`.

The split follows what each side legitimately knows:

- **Hardware facts** (`NUM_SIDES`, `PLAYER_COLORS`) `web_ui.cpp` reads directly
  from `octagon_core.h`. These describe the table, not the game.
- **Game facts** arrive only through an explicit interface. `web_ui` never sees
  `joinOrder`, `prevRosterMask`, or any other game internal, and the game never
  sees HTTP.

```cpp
// web_ui.h
struct TableState {
  uint8_t mode;               // index into modeNames
  uint8_t brightnessPercent;  // 5..100, retained while off
  bool    lit;                // false = table is off, game state preserved
  int8_t  currentSide;        // -1 when the mode has no single current seat
  uint8_t rosterMask;         // bit s = seat s is in
  uint8_t readyMask;          // bit s = seat s is green (READY only)
  bool    inSetupMode;
};

struct TableConfig {
  const char* const* modeNames;
  uint8_t modeCount;
};

typedef void (*StateReader)(TableState &out);
typedef bool (*ModeSetter)(uint8_t mode);           // false = valid but refused
typedef bool (*BrightnessSetter)(uint8_t percent);  // false = valid but refused
typedef bool (*PowerSetter)(bool lit);              // false = valid but refused

void webUiBegin(const TableConfig &cfg, StateReader read, ModeSetter setMode,
                BrightnessSetter setBrightness, PowerSetter setPower);
void webUiHandle();   // call from loop()
void webUiEnd();      // on Wi-Fi loss, symmetric with ArduinoOTA.end()
```

### Endpoints

Five, on port 80. No JSON parser on the device: requests carry query params,
responses are built with `snprintf` into a fixed `char[512]`. A JSON library
would cost more flash than it saves for six fields.

| Method | Path | Purpose |
|---|---|---|
| GET | `/` | the page, one self-contained HTML string in PROGMEM |
| GET | `/api/config` | static: mode names, seat colors, side count. Fetched once |
| GET | `/api/state` | dynamic: mode, brightness, lit, currentSide, roster, ready, inSetup. Polled every 2 s |
| POST | `/api/mode?value=0..3` | set mode |
| POST | `/api/brightness?value=5..100` | set brightness |
| POST | `/api/power?value=on\|off` | light the table or darken it |

Splitting config from state keeps the polled payload small (~150 bytes) and
keeps mode names and seat colors single-sourced from the firmware rather than
duplicated into the page.

Both POSTs return the updated state, so the page never has to guess whether a
change landed.

Status codes are split by *who* can tell what went wrong, because a single
`false` from a setter cannot distinguish two different failures:

- **`web_ui` validates range itself** — it knows `modeCount`, and the brightness
  bounds are its own contract — and returns **400** with a plain-text reason
  without ever calling the setter.
- **A `false` return from a setter therefore means "valid but refused right
  now"**, which is only ever the setup-session case, and becomes **409**.

### Lifecycle

`webUiBegin()` is called from the same place `beginOta()` is — the moment the
Wi-Fi link comes up, whenever that happens — and `webUiEnd()` on a drop, so the
server rebinds cleanly on reconnect. `webUiHandle()` goes in `loop()` next to
`handleSerial()`, and is skipped while `otaActive` is set.

`WebServer::handleClient()` is synchronous, so the page is unresponsive during
the blocking piezo remap wizard. That is acceptable: the wizard is a bench
operation with a 20 s timeout, and the operator is at the table with a cable.

## Brightness

`BRIGHTNESS` (128) becomes the *default* rather than the value. `octagon_core`
gains:

```cpp
uint8_t brightnessPercent();              // 5..100
void    setBrightnessPercent(uint8_t p);  // clamps, applies, persists
```

Percent is the stored unit — NVS `"octagon"/"bri"`, one byte — because both the
API and the UI speak percent, and storing FastLED's 0–255 would make the slider
round-trip lossy. Conversion is `raw = constrain(pct * 255 / 100, 13, 255)`.
Loaded in `octagonBegin()` next to the side table and piezo map, so `eight`
honours it too and it survives a reboot. Default is 50%, i.e. raw 127 — one step
off the 128 the table runs at today, which is imperceptible.

**The floor is 5%, not 0%.** Zero lets anyone at the table make it look broken,
and the code already states the principle that power is physical — plug in = on.
13/255 is dim but unmistakably lit.

Worth documenting in the UI copy: `MAX_POWER_MA` already auto-dims all-on
scenes, so raising brightness past roughly 60% visibly changes one-lit-side play
but not READY or the setup blink, which are already at the power cap.

## Off

A single `bool tableLit` in `turn_counter.ino`. Off means the LEDs are dark and
nothing else changes: mode, roster, whose turn it is, and everyone's ready flags
are all preserved and come straight back when it lights again. Brightness is
untouched, which is the whole reason this isn't the bottom of the slider.

**Off is deliberately not persisted.** Plug in = on, which is the principle the
code already states where it explains why there's no pair handler. A table that
boots dark after a power cut looks broken, and the person best placed to fix it
is standing next to a plug, not holding a phone.

### Turning it back on at the table

The phone can turn it off, and the phone may then leave the room. So there has
to be a way back that needs no phone.

**Four rapid taps on one side wakes it** — the same burst
`registerTapForSetupGesture()` already detects for entering setup. While off it
means wake; while lit it means setup. The two states are mutually exclusive, so
there's no ambiguity, and it costs nothing: no new detection code, no change to
tap timing.

Single taps do nothing while off, so knocking the table or setting a drink down
won't light it up.

Two alternatives were considered and rejected:

- **Any tap wakes it.** Simplest, but a game table gets bumped, and a table that
  relights every time someone leans on it isn't off in any useful sense.
- **The two-handed opposite-side slap.** This is what `PairHandler` in
  `octagon_core` was built for and it currently goes unused, so it's tempting.
  But installing a pair handler makes *every* tap wait out
  `OPPOSITE_PAIR_WINDOW_MS` (150 ms) to see whether its opposite is coming —
  the sketch comments call this out as the reason the handler is `nullptr`
  today. Swapping the handler in only while off would dodge the latency cost,
  but that's real state machinery to maintain for a gesture nobody has asked
  for, when a burst the firmware already recognises does the job.

### What it touches

- `commitTap()` while off feeds `registerTapForSetupGesture()` and returns,
  nothing else. The wake burst therefore lights the table without also passing
  a turn or toggling a ready flag — the taps that woke it are consumed by the
  waking.
- `renderCurrent()` and `startPlay()` call `renderOff()` while off.
- `loop()` skips the setup rendering while off — setup can't be entered anyway,
  since taps don't reach it.
- **Turning off during a setup session aborts it**, via the existing
  `abortSetupMode()`, which by design restores the previous roster and mode and
  writes nothing. So `setPower` always succeeds; it keeps the `bool` return only
  to match the other setters, and never produces a 409.
- OTA is unaffected: its progress bar deliberately lights the strip while off,
  because a silent OTA is worse than a brief unexpected glow, and the board
  reboots lit afterwards regardless.

## Mode changes

A phone mode change is not a setup session — it leaves the roster untouched.

```cpp
bool applyMode(uint8_t newMode) {
  if (newMode >= MODE_COUNT || inSetupMode) return false;   // don't fight a setup in progress
  gameMode = newMode;
  if (gameMode == MODE_ARB && joinCount == 0) rebuildJoinOrderFromRoster();
  if (currentSide < 0 || currentSide >= NUM_SIDES || !sideActive[currentSide]) {
    currentSide = (gameMode == MODE_ARB && joinCount > 0) ? joinOrder[0] : firstActiveSide();
  }
  prefs.putUChar("mode", gameMode);
  prefs.putUChar("curside", currentSide);
  startPlay();
  return true;
}
```

Rejecting the change while `inSetupMode` is deliberate: someone is mid-gesture
at the table, and a phone shouldn't yank the mode out from under the demo dial.
The API returns 409 in that case and the page says so.

`startPlay()` is correct here because it clears `ready[]`, which is what a mode
change should do.

### A hazard this design avoids

Brightness must **not** call `startPlay()`, because in READY mode that clears
every player's green. A new `renderCurrent()` redraws without touching state:

```cpp
void renderCurrent() {
  if (!tableLit) { renderOff(); return; }
  if (inSetupMode) return;            // setup re-renders every loop anyway
  if (readyMode()) renderReady(); else renderTurn();
}
```

`applyBrightness()` calls that, so the change is visible immediately without
resetting a ready-check in progress.

## The page

One self-contained HTML string — no external stylesheets, fonts or scripts. The
board has no internet path, so anything external simply would not load.

- Dark theme, large touch targets. It gets used in a dim room, one-handed.
- An on/off toggle, visually separated from the brightness slider so it doesn't
  read as part of it. While off, the mode buttons and slider stay live and
  usable — they just don't light anything until it's back on — and the page
  says the table is off rather than looking broken.
- Four mode buttons, the active one highlighted; names come from `/api/config`.
- Brightness slider, 5–100. Its value is unaffected by the off state.
- Status: current mode, whose turn it is (seat number and its colour swatch),
  roster as eight dots, ready/not-ready in READY mode, and a "setup in progress"
  banner when `inSetupMode` is set.
- Polls `/api/state` every 2 s. A failed poll shows a "table not responding"
  state rather than silently freezing stale values.

**Slider versus poll.** A naive implementation fights the user: they drag, a
poll lands, the slider jumps back. Two rules prevent it — POSTs are throttled to
one per 250 ms while dragging, and polled brightness is ignored for 2 s after
the last local change.

## Discovery

No single mechanism reaches every phone, so there are three layers:

1. **mDNS** — `turn-counter.local`, already working. Covers Apple devices.
2. **A stable IP** — a one-time DHCP reservation on the router. Preferred over a
   static IP in firmware because the router then guarantees no conflict with its
   own pool. The firmware prints `WiFi.macAddress()` at boot so the MAC is to
   hand when making the reservation.
3. **A QR sticker for under the table** — `make qr URL=...` generates a
   printable SVG and PDF encoding the table's URL, through the same weasyprint
   pipeline the other docs use. Adds one pure-Python dependency, `segno`.

`scripts/make_qr.py` exposes `normalize_url()` as a pure function — bare host to
`http://host/`, existing scheme preserved, empty or malformed rejected — which
is the part worth testing off-board.

## Testing

The split is the same as the OTA work, and the reason is worth restating: the
last round's unit tests passed while a protocol assumption underneath them was
wrong, and only a real push found it.

- **Automated (pytest):** `normalize_url()` edge cases; QR generation writes a
  non-empty SVG.
- **Compile-gated:** `make compile-all`. Flash use is expected to rise from 67%
  to roughly 75% of the min_spiffs app slot; anything above 90% should be
  reported rather than absorbed.
- **Bench, protocol first.** Before trusting the page, hit the API directly, so
  a page bug and an API bug can't hide each other:

  ```bash
  curl -s http://turn-counter.local/api/config
  curl -s http://turn-counter.local/api/state
  curl -s -X POST "http://turn-counter.local/api/mode?value=1"        # -> CCW
  curl -s -X POST "http://turn-counter.local/api/brightness?value=20"
  curl -s -X POST "http://turn-counter.local/api/power?value=off"
  curl -s -X POST "http://turn-counter.local/api/power?value=on"
  curl -s -X POST "http://turn-counter.local/api/mode?value=9"        # -> 400
  curl -s -X POST "http://turn-counter.local/api/brightness?value=0"  # -> 400
  curl -s -X POST "http://turn-counter.local/api/power?value=maybe"   # -> 400
  ```
- **Bench, behaviour.** Brightness change during a READY round must not clear
  anyone's green. A mode change during a tap-setup session must be rejected with
  409. Brightness must survive a power cycle. The page must work from both an
  iPhone (via `.local`) and an Android phone (via IP).
- **Bench, off.** Turn the table off mid-game, then back on from the phone —
  the same seat's turn resumes, and in READY mode everyone's green survives.
  Single-tap several sides while off and confirm nothing lights. Then wake it
  with four rapid taps on one side and confirm it comes back to the same state,
  and does *not* land in setup mode. Power-cycle while off and confirm it boots
  lit, not dark.

## Risks

- **Flash headroom.** ~626 KB free today; the server and page should use a
  fraction. If it doesn't fit, the fallback is trimming the page, not changing
  the partition scheme — `min_spiffs` is already the larger option and its dual
  OTA slots are load-bearing.
- **A blocking handler stalls the game loop.** Handlers must not delay; the
  heaviest does an `snprintf` and a `Preferences` write.
- **Guest access is unauthenticated by design.** Documented, and the exposed
  surface is limited to two reversible settings.

## Out of scope

- Roster editing, per-seat colours, turn timers, scorekeeping.
- Serving the page over TLS, or from SPIFFS rather than PROGMEM.
- Any change to tap detection, the piezo map, or OTA.
