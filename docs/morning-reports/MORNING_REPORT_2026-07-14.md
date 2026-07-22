# Morning brief — 2026-07-14

Evening review of the whole repo (firmware ×4 sketches, design doc, dry run, shopping list, all 8 figures, build scripts, Makefile, git state, project memory). Compile gate: **all four sketches build clean, 0 warnings** (`make compile-all`; turn_counter = 67% of the min_spiffs partition). `main` is fully pushed; no stashes, no stray branches. No CRITICAL findings.

## Top of stack (do this first)

1. **Commit the 5-day backlog** (~20 min). Two logical bundles are sitting uncommitted: (a) the tap-light doc bundle from Jul 7–9 (`tap_light_circuit.md/.pdf`, `doc-src/build_tap_light_pdf.py`, `doc-src/tap_light_layout.svg`, Makefile `pdf` target, `tap_light.ino` retune) and (b) this week's Phase-5 work (protoboard figures, BOM/README/design-doc edits). A fresh clone today has a broken `make pdf` and missing Phase-5 printouts because committed-doc references point at untracked files. While you're there: `.gitignore` the build artifacts (swap the two exact `_build_*.html` entries for `doc-src/_build_*.html`) and delete the stray 581 KB `protoboard_layout.svg.png` at repo root (my render accident from Jul 9 — sorry).
   ⚠️ One flag before committing: the working-tree `tap_light.ino` sets `TAP_DELTA 1000 / DEBOUNCE 500`, which exactly **reverts commit `3fa189d` "more sensitive tap light"** (500/250). The uncommitted `tap_light_circuit.md` documents 1000/500, so doc+code on disk agree — but say so in the commit message ("revert over-sensitive tuning") so history isn't confusing.
2. **Fix the silent no-op in `build_pdf.py`** (~15 min). [doc-src/build_pdf.py:52-71](doc-src/build_pdf.py#L52-L71): the `companion_block_md` literal no longer matches the design doc (the md gained a `breadboard_layout.svg` bullet), so the `.replace()` finds nothing — **every `make pdf` since then has rendered the companion callout unstyled, silently**, including this week's committed PDF. Fix the two string literals and add `assert companion_block_md in md_text` (ditto `title_block_md` and the heading anchors) so anchors can never rot silently again. Rebuild PDFs after.
3. **One-liner doc sweep** (~30 min) — all the small drift in one commit; list under MEDIUM below (items M4–M11).

## Findings — HIGH

- **H1 · One stuck-high piezo takes down tap input for the whole table.** [firmware/turn_counter/turn_counter.ino:141-154](firmware/turn_counter/turn_counter.ino#L141-L154). Readings above threshold never feed the baseline average, so the baseline can adapt down but never *up past TAP_DELTA*. A permanent DC shift on one side (the floating-pin/lost-ground failure you already hit on the bench 2026-07-07) makes that side spike forever: it wins every scan, fires a ghost tap every 250 ms, and while inside its debounce window the `return 0` at :153 throws away the entire scan — **dropping all 8 sides' real taps**. If the stuck side is unlit, the ghosts also feed the 4-tap setup gesture. Fix sketch: rate-limited upward baseline slew after ~2 s continuously above threshold; consider excluding continuously-high sides from winner selection. (Design the fix together with L6 — the same `return 0` is what suppresses cross-talk ghosts during ringing.)
- **H2 · `build_pdf.py` companion-block anchor silently broken** — top-of-stack item 2 above.
- **H3 · Uncommitted-work exposure** — top-of-stack item 1 above ([git status](.): 5 modified + 8 untracked files, spanning two sessions).

## Findings — MEDIUM

- **M1 · Setup gesture counts taps from *any* unlit side collectively.** [turn_counter.ino:189-197](firmware/turn_counter/turn_counter.ino#L189-L197) (+ call at :248). The burst counter never records which side tapped — two players drumming different dark sides can jointly trip setup mode mid-game, increment the player count, and the 3 s idle exit **silently persists** the corrupted count. Fix: store the burst's side; reset the counter when a different side taps.
- **M2 · Slap-off during setup mode abandons unsaved playerCount.** [turn_counter.ino:214-229](firmware/turn_counter/turn_counter.ino#L214-L229). `toggleOnOff()` clears `inSetupMode` without persisting (or reverting) `playerCount` — the table plays with the new count until a power cycle silently reverts it. Fix: persist or reload in `toggleOnOff` when leaving setup.
- **M3 · Real OTA password committed + doc disagrees.** [turn_counter.ino:20](firmware/turn_counter/turn_counter.ino#L20) ships `OTA_PASSWORD = "letsplayagame"` (committed in `fb9aff5`); design doc :588 claims the default is `change-me`. Wi-Fi SSID/password are still placeholders (clean). LAN-only threat model, but if this repo is/goes public the credential is live. Fix: move creds to a gitignored `secrets.h`, rotate, fix the doc row.
- **M4 · Old "data + ground" 3-pin language survives in Phase 6b.** [turn_counter_design_doc.md:516](turn_counter_design_doc.md#L516) — the only survivor of last week's harness change; §2/§5/figures all say 5V+data+ground now.
- **M5 · Tap Light TAP_DELTA default wrong in design doc.** [turn_counter_design_doc.md:191](turn_counter_design_doc.md#L191) says 1500; on-disk firmware and `tap_light_circuit.md:186` say 1000.
- **M6 · min_spiffs partition requirement missing from the design doc.** §7 build environment (:592) and the §8 flash-fails row never mention Partition Scheme → Minimal SPIFFS, but turn_counter *cannot link* on the default partition (README/Makefile/bench memory all know this; an IDE-only reader doesn't). Add to §7 + a troubleshooting row.
- **M7 · §8 troubleshooting is missing the ground-fault lesson from the bench.** The 2026-07-07 failure (chaotic taps whether the piezo is plugged or not, rail-to-rail readings with a stable baseline = floating ADC pin / broken GND return — *not* fixable by raising TAP_DELTA) lives only in session memory. Add the row; it's the highest-value trap this project has actually hit.
- **M8 · Shopping list header says Amazon = "Seven items"; the table now has twelve.** [shopping_list.md:12](shopping_list.md#L12).
- **M9 · README says `make pdf` rebuilds "both" PDFs; it builds three.** [README.md:82](README.md#L82), :31, and the build section (:108-114) never mention `build_tap_light_pdf.py` / `tap_light_circuit.pdf`.
- **M10 · README repo-layout tree is stale.** [README.md:9-39](README.md#L9-L39) omits `tap_light_circuit.md/.pdf`, `doc-src/build_tap_light_pdf.py`, `doc-src/tap_light_layout.svg`, and `scripts/gen_intellisense.py` (which `make vscode` depends on).
- **M11 · The new protoboard figures describe the BOM as still un-fixed.** [doc-src/protoboard_layout.svg:273,275](doc-src/protoboard_layout.svg#L273): "the BOM's 5×7 cm board is too small… the BOM's ~110×60×30 mm box can't hold it" — but the BOM was updated the same day, so the printed figure contradicts the doc it ships with. Reword to "the Phase 0 starter's board / an earlier draft's box".
- **M12 · BOM specs 18 AWG power wire that no purchase list sells.** [turn_counter_design_doc.md:259](turn_counter_design_doc.md#L259) — actual purchases are 14 AWG (DC runs) and 20 AWG (board pigtail). Change the row to 20 AWG.
- **M13 · DIP-14 socket and M3 standoffs exist only in the shopping list.** Absent from §2 BOM and the Phase 5 checklist (which still says solder order without the socket and "mount protoboard" without standoffs).
- **M14 · The purchased BB400 breadboard cannot host the documented Phase 2 layout.** `breadboard_layout.svg:237` itself says 30 columns is too short (needs ≥40; assumes the ELEGOO 830-pt board), but §2 (:267) and Phase 2 still point at the half-size board with no caveat.

## Findings — LOW

- **L1 ·** Setup-mode blink preview places swatches with different math than actual wedge assignment ([turn_counter.ino:107](firmware/turn_counter/turn_counter.ino#L107) vs :78-90) — for 3/5/6/7 players the blinking sides aren't the sides those players will own. Cosmetic but confusing during setup.
- **L2 ·** NVS write on every turn advance, un-debounced ([turn_counter.ino:186](firmware/turn_counter/turn_counter.ino#L186)). Decades-safe at human tap rates; dirty-flag + idle flush if you ever care.
- **L3 ·** `renderSetup()` pushes a full 240-LED frame every loop pass ([turn_counter.ino:384-390](firmware/turn_counter/turn_counter.ino#L384-L390)) — ~7 ms per frame, roughly halving piezo scan rate while setup mode is active. Only `show()` on blink flips.
- **L4 ·** hello_board comment says "twice a second"; the loop delays 10 s ([hello_board.ino:6](firmware/hello_board/hello_board.ino#L6) vs :52).
- **L5 ·** Very soft opposite-side slaps can lose the second hand to the first side's ringing (`return 0` at :153-154); usually rescued by the 150 ms pending window. Fold into the H1 rework — don't fix naively or cross-talk ghosts return.
- **L6 ·** Figure-to-figure nit: placement says pigtail "(18–20 AWG)" ([protoboard_layout.svg:92](doc-src/protoboard_layout.svg#L92)), wiring sheet says "(20 AWG)". Harmonize to 20 AWG.
- **L7 ·** Power-budget prose says "never light more than ~30 LEDs" ([turn_counter_design_doc.md:341](turn_counter_design_doc.md#L341)); the default 4-player layout lights 60 (2-player: 120). Conclusion unchanged; number wrong.
- **L8 ·** §4.6 says buy the grommet first, then drill to match (:427); Phase 6b (:509) presupposes ½″. Align.
- **L9 ·** "Littlefuse" → **Littelfuse** ([shopping_list.md:78](shopping_list.md#L78)) — hurts part-number search.
- **L10 ·** Design doc companion block + §0.5 never mention `tap_light_circuit.md/.pdf` or the Phase-5 printouts; if you add bullets, do it in the same commit as the `build_pdf.py` anchor fix (H2) or the block breaks again.
- **L11 ·** `make pdf` hardcodes `.venv/bin/python3` with no existence guard — cryptic failure on a fresh clone; `make vscode` uses bare `python3`. Add a guard line.
- **L12 ·** Design doc cites `turn_counter_wiring.svg` without the `doc-src/` prefix (:12, :287, :441) while the newer figure references carry it.
- **L13 ·** §8's "won't flash via USB" row says confirm **USB CDC On Boot: Enabled** — true for the IDE/native-USB path, but the bench flow (README + memory) flashes the CP2102 `usbserial` port with CDC *disabled*. Add which-port framing so the row doesn't send you the wrong way.

## Verified OK (so you don't re-litigate)

- Tap pipeline core: adaptive baselines (fixed-point, overflow/underflow-safe), millis-wraparound safety, winner rule, opposite-pair logic, pending-commit ordering, 4-tap gesture constants — all match doc §6/§7.
- All FastLED ranges bounded at NUM_LEDS for every player count; OTA progress math safe.
- ADC1 discipline holds pin-for-pin (GPIOs 1,2,4–9; GPIO 3 correctly skipped); Wi-Fi/OTA never blocks `loop()`; table plays with Wi-Fi down.
- NVS persistence real and validated on restore (players clamped 2–8).
- Shopping-list arithmetic verified row-by-row: every subtotal and the $320–330 / $633–648 totals check out; §2 BOM quantities agree with the purchase lists.
- Pin maps consistent across firmware = §3.1 = Figure 3.1 = both protoboard figures = breadboard figure; `dry_run.md` clean (no stale constant names); Makefile ↔ README shortcut table matches.
- `tap_light_circuit.md` line references verified against the on-disk sketch, all accurate.
- Memory threads graded: flashing-bench + partition-scheme notes already encoded in README/Makefile (done); ground-fault lesson NOT yet in §8 (→ M7).

## Open loops carried forward

- Firmware robustness work (H1 + M1 + M2 + L1/L3/L5) is a single coherent bench session — needs the table powered to validate; not attempted tonight (read-only).
- OTA password rotation (M3) pending your call on repo visibility.
- Whether to add a §8 row for the LED-JST-unplugged-while-USB-flashing case (wiring-sheet note exists; doc row doesn't).

## Decisions deferred

- **Is this GitHub repo public or private?** Couldn't check without `gh` (not in tonight's allowlist). It changes M3's urgency: public → rotate the OTA password now; private → housekeeping. Provisional: treat as private, fix in the doc sweep.
- **Is tap_light 1000/500 the intended final tuning?** Working tree + doc agree, commit history disagrees. Provisional: yes — commit it with an explanatory message (top-of-stack 1).

## What I did NOT do tonight

Read-only run: no commits, no pushes, no fixes, no PDF regeneration (note: the on-disk `turn_counter_design_doc.pdf` therefore still carries H2's unstyled companion block). Build gate (`make compile-all`) and scratchpad SVG renders were the only executions. One file was created before you left, with approval: `.claude/settings.local.json` (tonight's read-only allowlist) — keep or delete as you like. This brief is the only other file written.
