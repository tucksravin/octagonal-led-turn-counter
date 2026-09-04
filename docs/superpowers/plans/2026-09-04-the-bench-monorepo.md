# the-bench Monorepo Migration — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create `~/Documents/GitHub/the-bench` and migrate `octagonal-led-turn-counter` into it with full git history, establishing a shared bench layer that future workshop projects join.

**Architecture:** A monorepo whose root holds shared workshop context (inventory, bench lore, conventions, the adding-a-project procedure) and whose `projects/` subdirectories hold self-contained builds. The migrated project is byte-identical inside — its `Makefile`, `.venv` and `requirements.txt` stay project-local and untouched. History comes across via `git subtree` with no squash.

**Tech Stack:** git subtree, GNU make (delegation via `$(MAKE) -C`), Python venv, arduino-cli, `gh` CLI.

**Spec:** [`docs/superpowers/specs/2026-09-04-the-bench-monorepo-design.md`](../specs/2026-09-04-the-bench-monorepo-design.md)

---

## Path shorthand used throughout

| Alias | Real path |
|---|---|
| `$OLD` | `/Users/tuckerlemos/Documents/GitHub/octagonal-led-turn-counter/octagonal-led-turn-counter` |
| `$WRAP` | `/Users/tuckerlemos/Documents/GitHub/octagonal-led-turn-counter` (the wrapper — **not** a git repo) |
| `$BENCH` | `/Users/tuckerlemos/Documents/GitHub/the-bench` |
| `$PROJ` | `$BENCH/projects/octagonal-led-turn-counter` |
| `$OLDSLUG` | `~/.claude/projects/-Users-tuckerlemos-Documents-GitHub-octagonal-led-turn-counter-octagonal-led-turn-counter` |
| `$NEWSLUG` | `~/.claude/projects/-Users-tuckerlemos-Documents-GitHub-the-bench` |

## Ordering constraints — do not reorder

1. **Task 1 before Task 5.** Cleanups must be committed in the old repo so they ride the subtree into history.
2. **Task 5 before Task 6.** `git subtree add` fails if the prefix directory already exists; the hand-copies create files under it.
3. **Task 6 before Task 7.** `make test` needs the tree in place.
4. **Task 11 (board gates) before Task 12 (deletion).** Nothing on the laptop proves `secrets.h` landed intact. The wrapper is the only rollback until OTA succeeds.

**Task 11 is run by the user at the bench.** Everything else is laptop-only.

---

## Task 1: Pre-migration cleanups in the current repo

**Files:**
- Modify: `$OLD/tests/test_ota_flash.py:11,14`
- Modify: `$OLD/.gitignore:3-4`
- Delete: `$OLD/protoboard_layout.svg.png`

Prescribed by `docs/morning-reports/MORNING_REPORT_2026-07-14.md:7`, plus the fixture swap from spec §4.5. Done first so it lands in the history that migrates.

- [ ] **Step 1: Swap the retired credential out of the test fixture**

The string `letsplayagame` is a dead OTA password (current firmware defaults `OTA_PASSWORD ""` at `turn_counter.ino:25`), but it reads as a live credential in a public repo. Replace both occurrences:

```bash
cd $OLD
sed -i '' 's/letsplayagame/not-a-real-password/g' tests/test_ota_flash.py
grep -n "not-a-real-password\|letsplayagame" tests/test_ota_flash.py
```

Expected: two lines showing `not-a-real-password`, zero showing `letsplayagame`.

- [ ] **Step 2: Run the test suite to confirm the swap is self-consistent**

```bash
.venv/bin/python3 -m pytest tests/test_ota_flash.py -v
```

Expected: PASS. The fixture defines the value and the assertion checks it, so both sides change together.

- [ ] **Step 3: Delete the stray render artifact**

581 KB PNG at the repo root, a render accident from Jul 9. Confirm nothing references it, then remove:

```bash
git grep -l "protoboard_layout.svg.png" -- . ':!docs/morning-reports/*'
```

Expected: **no output** (the only reference is the morning report that prescribes the deletion).

```bash
git rm protoboard_layout.svg.png
```

- [ ] **Step 4: Fix the `_build_*.html` gitignore inconsistency**

Currently two exact paths are ignored while three siblings are tracked. Replace lines 3-4 of `.gitignore`:

```bash
sed -i '' 's|^doc-src/_build_doc\.html$|doc-src/_build_*.html|' .gitignore
sed -i '' '/^doc-src\/_build_dry_run\.html$/d' .gitignore
sed -n '1,6p' .gitignore
```

Expected: line 3 is now `doc-src/_build_*.html`, the `_build_dry_run.html` line is gone.

- [ ] **Step 5: Untrack the three now-ignored build artifacts**

```bash
git rm --cached doc-src/_build_bench_guide.html doc-src/_build_simple.html doc-src/_build_tap_light.html
git status --short
```

Expected: three `D` entries staged, the files still present on disk.

- [ ] **Step 6: Full suite green before committing**

```bash
.venv/bin/python3 -m pytest tests/ -q
```

Expected: `53 passed`.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "cleanup: clear the 2026-07-14 morning report's standing to-dos

- widen the .gitignore _build_*.html entries; three siblings were tracked
  while two exact paths were ignored, which was just inconsistent
- delete the stray 581 KB protoboard_layout.svg.png (render accident from
  Jul 9, referenced nowhere outside the report that flagged it)
- swap the retired 'letsplayagame' OTA password out of the test fixture; it
  is dead (turn_counter.ino:25 defaults OTA_PASSWORD \"\") but reads as a live
  credential in a public repo

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 8: Push — this is the pre-move backup**

```bash
git push origin main
git rev-list --left-right --count origin/main...main
```

Expected: `0	0`. GitHub now holds a complete copy before anything moves.

---

## Task 2: Quiesce and baseline

**Files:** none modified. This task only records state and takes a backup.

- [ ] **Step 1: Close everything holding the old path**

Manual, and it matters — memory or edits written after the copy in Task 9 are silently lost at Task 12's deletion:
- Close the VS Code window opened on `$OLD`
- Close any `arduino-cli monitor` (it holds `/dev/cu.usbserial-*` exclusively)
- End any other Claude session whose cwd is `$OLD`

```bash
lsof /dev/cu.usbserial-* 2>/dev/null || echo "port free"
```

Expected: `port free`.

- [ ] **Step 2: Record the live baseline**

The spec's numbers were measured before the spec and cleanup commits landed. **These values, not the spec's, are the gates.**

```bash
cd $OLD
echo "tip:      $(git rev-parse main)"
echo "roottree: $(git rev-parse main^{tree})"
echo "rootcmt:  $(git rev-list --max-parents=0 main)"
echo "commits:  $(git rev-list --count main)"
echo "merges:   $(git rev-list --merges --count main)"
echo "tracked:  $(git ls-files | wc -l | tr -d ' ')"
.venv/bin/python3 -m pytest tests/ -q 2>&1 | tail -1
```

Write these down. Invariants that must **not** change across the migration: `rootcmt` = `566366eb62cf370e71340c20dbfcdd5dc03e04dd`, `merges` = 0, tests = `53 passed`.

- [ ] **Step 3: Verify object integrity before a one-way move**

```bash
git fsck --no-progress 2>&1 | grep -v '^dangling' ; echo "[end of errors]"
```

Expected: nothing between the command and `[end of errors]`. Dangling *blobs* are normal edit residue; dangling *commits* would mean lost work and must be investigated before proceeding.

- [ ] **Step 4: Dated backup outside the migration area**

```bash
tar -czf ~/Desktop/octagonal-led-turn-counter-premove-$(date +%Y%m%d).tar.gz \
  --exclude='.venv' --exclude='build' --exclude='__pycache__' \
  -C /Users/tuckerlemos/Documents/GitHub octagonal-led-turn-counter
ls -lh ~/Desktop/octagonal-led-turn-counter-premove-*.tar.gz
```

Expected: an archive of roughly 15–20 MB (the ~180 MB of build output and the 81 MB venv are excluded; `.git` and all untracked keepers are included). **Retain until a week after Task 11 passes.**

---

## Task 3: Delete the stray `erp-industrial` fork

**Files:**
- Delete: `$WRAP/erp-industrial/`

Spec §4.1. This is a deletion, so re-verify redundancy immediately before rather than trusting the audit.

- [ ] **Step 1: Re-verify the fork holds nothing unique**

```bash
A=$WRAP/erp-industrial
B=/Users/tuckerlemos/Documents/GitHub/erp-industrial
echo "stray remote:  $(git -C $A remote get-url origin)"
echo "live remote:   $(git -C $B remote get-url origin)"
echo "dirty:         [$(git -C $A status --porcelain --ignored -uall)]"
echo "stash:         [$(git -C $A stash list)]"
echo "unpushed:      [$(git -C $A log origin/main..HEAD --oneline)]"
git -C $B merge-base --is-ancestor $(git -C $A rev-parse HEAD) main \
  && echo "ANCESTOR: yes — nothing unique, safe to delete" \
  || echo "ANCESTOR: NO — STOP, the fork has unique commits"
```

Expected: stray remote is `tucksravin/…`, live remote is `reddoorla/…`, all three bracketed values empty, and `ANCESTOR: yes`.

**If `ANCESTOR: NO` — stop and reassess. Do not delete.**

- [ ] **Step 2: Delete**

```bash
rm -rf $WRAP/erp-industrial
ls $WRAP
```

Expected: only `octagonal-led-turn-counter` (and possibly `.DS_Store`) remains.

> **Never `mv` this to `/Users/tuckerlemos/Documents/GitHub/erp-industrial`** — that path holds the live Reddoor client site with its `.env`. `mv` onto an existing directory nests inside it.

---

## Task 4: Create `the-bench` and its shared layer

**Files:**
- Create: `$BENCH/README.md`, `$BENCH/CLAUDE.md`, `$BENCH/Makefile`, `$BENCH/.gitignore`, `$BENCH/.gitattributes`
- Create: `$BENCH/bench/lore.md`, `$BENCH/bench/conventions.md`, `$BENCH/bench/adding-a-project.md`

Written **before** the subtree so the first commit already carries the shared layer — and before Task 12, while the old memory directory is still reachable.

- [ ] **Step 1: Initialise the repo**

```bash
test -e $BENCH && echo "OCCUPIED — STOP" || mkdir -p $BENCH
cd $BENCH && git init -b main
```

Expected: `Initialized empty Git repository`. (`init.defaultBranch` is already `main` on this machine; `-b main` is explicit anyway.)

- [ ] **Step 2: Write `$BENCH/CLAUDE.md`**

```markdown
# the-bench

Monorepo for physical/bench builds. Every project under `projects/` is
self-contained — its own `Makefile`, `.venv`, `requirements.txt`, tests and
docs. This root holds only what is genuinely shared across builds.

## Projects

| Project | What it is |
|---|---|
| `projects/octagonal-led-turn-counter/` | ESP32-S3 LED rim turn counter for an octagonal gaming table — 8 piezo seats, WS2812B rim, Wi-Fi phone control |

`ls projects/` is the live index. Read a project's own `README.md` before
working in it.

## Don't run commands that touch the board

The user drives anything that reaches the physical bench. The line is whether
the command touches hardware on the table, not whether it is Bash.

- **Never run:** `make flash-*`, `make ota`, `make monitor`, `make ping`,
  `make record`, `make map-piezos`, pyserial probes, HTTP calls to the board.
- **Fine to run:** git, file moves, `make test` (pytest, no board),
  `make compile-all` / `arduino-cli compile` (toolchain only), `make pdf`.

Verify what you can on the laptop, then hand over the board-touching step:
"flash with `make flash-turn` and tell me what the serial says."

Board HTTP/ping additionally needs a sandbox with LAN access — the plain Bash
tool's sandbox blocks outbound LAN **silently**, which looks exactly like a
dead board. See `bench/lore.md`.

## Specs and plans go with their project

The superpowers skills default to a repo-root `docs/superpowers/{specs,plans}/`.
In this repo that is wrong for project work — write to
`projects/<name>/docs/superpowers/{specs,plans}/` instead, so specs stay beside
the code they describe. `writing-plans/SKILL.md:19` sanctions this override
explicitly ("User preferences for plan location override this default").

The bench-root `docs/superpowers/` is reserved for genuinely cross-project
work — the monorepo itself, shared tooling, bench-wide conventions.

## Running project commands

Bench scripts are **cwd-sensitive**: `make_qr.py` and `record_piezos.py` write
to paths relative to the working directory. Always drive them through make, from
the bench root or the project directory — never by path from the root, which
would scatter project artifacts into the monorepo root where the project's
`.gitignore` cannot reach them.

    make test                    # forwards to the default project
    make PROJ=<name> test        # another project
    make -C projects/<name> test # explicit; always works

After a fresh clone there is no `.venv`. Create one before any Python target:

    cd projects/<name> && python3 -m venv .venv \
      && .venv/bin/python3 -m pip install -r requirements.txt

## Keep the inventory current

`inventory.md` at this root tracks parts bought, consumed, and **spare** across
every bench project — the point is knowing surplus (100-pack resistors, the
530-pc JST kit, leftover WS2812B strip) so a new project doesn't re-buy what is
already in a drawer. When parts are bought, consumed, or drawn from stock by a
new project, update it.
```

- [ ] **Step 3: Write `$BENCH/Makefile`**

Uses `$(MAKE) -C`. **Never `make -f`** — `-f` expands the project's recipes correctly under `make -n` but then runs them with cwd at the bench root, where neither `.venv/` nor `tests/` exists, breaking all ~20 targets at once.

```makefile
# the-bench — thin delegator.
#
# Every project under projects/ is self-contained: its own Makefile, .venv and
# requirements.txt. This file forwards to them and owns nothing else.
#
#   make test                     -> make -C projects/octagonal-led-turn-counter test
#   make PROJ=<name> test         -> same, for another project
#   make -C projects/<name> test  -> the explicit form; always works
#
# Uses $(MAKE) -C, never `make -f`. With -f, make would expand the project's
# recipes but run them with cwd HERE, where .venv/ and tests/ do not exist —
# breaking every target at once while looking correct under `make -n`.

PROJ ?= octagonal-led-turn-counter

.PHONY: help projects

help: ## this message
	@echo "the-bench — workshop monorepo"
	@echo ""
	@echo "  make projects              list bench projects"
	@echo "  make <target>              run <target> in $(PROJ)"
	@echo "  make PROJ=<name> <target>  run <target> in another project"
	@echo ""
	@echo "Per-project targets:  make -C projects/$(PROJ) help"

projects: ## list bench projects
	@ls -1 projects/

# Forward anything else to the project's own Makefile.
%:
	@$(MAKE) -C projects/$(PROJ) $@
```

> Note the useful side effect: from the bench root the command string stays
> `make compile-all`, so the existing `Bash(make compile-all:*)` permission rule
> keeps matching. Only the explicit `make -C …` form needs a new rule.

- [ ] **Step 4: Write `$BENCH/.gitignore`**

Small and defensive. **Never** copy the project's slash-bearing rules up — they would anchor to the wrong directory.

```gitignore
# Finder noise — the project-scoped .gitignore no longer covers this level
.DS_Store
**/.DS_Store

# Insurance against cwd mistakes: bench scripts are cwd-sensitive, and a
# hand-run make_qr.py / record_piezos.py from this root would scatter project
# artifacts here, outside the reach of the project's own .gitignore.
.venv/
build/
data/
__pycache__/
*.pyc
.pytest_cache/
table_qr.pdf
```

- [ ] **Step 5: Write `$BENCH/.gitattributes`**

```gitattributes
# Normalise line endings on checkin, tree-wide.
* text=auto
```

- [ ] **Step 6: Write `$BENCH/README.md`**

```markdown
# the-bench

Workshop monorepo — physical/bench builds and the shared context behind them.

## Projects

| Project | What it is |
|---|---|
| [`projects/octagonal-led-turn-counter/`](projects/octagonal-led-turn-counter/) | ESP32-S3 LED rim turn counter for an octagonal gaming table |

## Shared layer

| Path | What it holds |
|---|---|
| [`inventory.md`](inventory.md) | Parts bought, consumed and **spare** across every project — check before re-buying |
| [`bench/lore.md`](bench/lore.md) | Hard-won bench facts: flashing, serial, power, failure signatures |
| [`bench/conventions.md`](bench/conventions.md) | How a bench project is laid out |
| [`bench/adding-a-project.md`](bench/adding-a-project.md) | Procedure for bringing another repo in with its history |

## Running things

Each project is self-contained — its own `Makefile`, `.venv` and
`requirements.txt`. The root `Makefile` only forwards:

    make help                     # what this root offers
    make test                     # runs in the default project
    make -C projects/<name> help  # that project's own targets

After a fresh clone, create the project's venv before any Python target:

    cd projects/<name>
    python3 -m venv .venv && .venv/bin/python3 -m pip install -r requirements.txt
```

- [ ] **Step 7: Write `$BENCH/bench/lore.md`**

This is the durable copy of context that otherwise lives only in path-keyed auto-memory. Write it **now**, while `$OLDSLUG/memory/` is still reachable.

```markdown
# Bench lore

Hard-won facts from real bench sessions. Committed here because the auto-memory
that held them is keyed to an absolute path and does not survive a move or a new
machine.

## Flashing an ESP32-S3 from this Mac

- **The board is the `/dev/cu.usbserial-*` port** (CP2102N UART bridge). Use the
  **UART** micro-USB jack, not the native-USB one, for flashing and the serial
  monitor. Set **USB CDC On Boot: Disabled** so `Serial` routes to the CP2102.
- **The `/dev/cu.usbmodem*` ports are the LG monitor's USB controls, not a
  board.** Selecting one gives esptool a real port that never answers →
  `Failed to connect to ESP32-S3: No serial data received`. Check the port
  before anything else when you see that error.
- **Charge-only cables** are the other classic: the board powers up and runs
  (onboard GPIO48 RGB cycles rainbow on factory firmware) but nothing
  enumerates. Needs a data micro-USB cable.
- **If auto-reset fails on the correct port:** hold BOOT, tap RST, release BOOT
  to enter download mode (rainbow stops = you're in the bootloader). If
  esptool's own reset still knocks it out, hold BOOT through the entire
  "Connecting…" phase and release once "Writing at 0x…" appears. Drop upload
  speed to 115200 if a hub can't sustain 921600.
- A USB hub is fine — the board has only micro-USB, so it can't reach this Mac
  without a C-to-micro cable anyway.

## Partition scheme

`turn_counter.ino` links to ~1,320,000 bytes — about 9 KB over the default
1.25 MB app partition — so a plain `--fqbn esp32:esp32:esp32s3` **fails** with
"text section exceeds available space". Verified 2026-07-05 on esp32 core
3.3.10 / FastLED 3.10.5.

Use `--fqbn esp32:esp32:esp32s3:PartitionScheme=min_spiffs` (1.9 MB app, keeps
the OTA partition — required, the sketch uses ArduinoOTA). `huge_app` also links
but has no OTA partition. The same FQBN must be used for the upload. `tap_light`
and `strip_test` fit the default fine (~52%).

## Serial workflow

- `make monitor` (arduino-cli) is **line-buffered** — typed characters reach the
  firmware only on Enter. Single-char commands need `0⏎`, `+⏎`. "Typing
  commands with no effect" usually means no Enter, or no monitor open.
- An open monitor holds the port exclusively; `make flash-*` fails with
  "Resource busy" until it's closed. A retry loop
  (`until make flash-tap; do sleep 5; done`) flashes the moment the port frees.
- To probe running firmware **without resetting it**: pyserial with
  `dtr = False` and `rts = False` set **before** `open()`. Otherwise opening the
  port toggles DTR/RTS and reboots the ESP32.

## Reading the board over the network

Board HTTP (`/api/diag`, `/api/state`) and ICMP need a sandbox with LAN access.
A plain Bash sandbox blocks outbound LAN **silently** — a poller once logged
268/268 missed polls against a board that was answering fine, which read as a
dead board and cost a diagnostic window. The failure is indistinguishable from
an offline board unless you already know the cause.

Board was 192.168.0.50 via DHCP (may change). `turn-counter.local` resolves on
macOS, not Android. `make ping` needs USB and resets the board.

## Failure signature: chaotic taps = a floating ADC pin

**Symptom:** continuous chaotic taps whether or not a piezo is plugged in.
Serial shows a stable low `baseline` (~100) but `reading` swinging rail-to-rail
to 4095.

**Cause (2026-07-07, and again 2026-08-16 on side 7):** a lost ground return.
The piezo front-end (1 MΩ pulldown + 3.3 V Zener, both to GND) loses its path
back to ESP32 GND, so the input floats and the ADC reads rail-to-rail noise.

**Why you cannot fix it with a threshold:** the detector trips at
`baseline + TAP_DELTA`; a floating high-impedance node saturates the rail, far
above any threshold. A stable low baseline proves the 1 MΩ pulldown IS
connected — rail-to-rail readings then mean the pin still floats. Suspect the
ground return or a degraded contact, never the code.

**Diagnostic:** pull the piezo (+) lead, leave the 1 MΩ + Zener. If it still
rails, the noise is on the input node or ground, not the piezo. Note the 1 MΩ
lives at the *disc* on the installed table, so unplugging a disc's JST removes
the pulldown and **creates** this fault.

**Since 2026-08-16** the tap guard auto-quarantines such a side by loudness duty
cycle (loud ≥~38% of the last second → muted; `/api/diag` reports `"muted":1`,
red row on the phone page; unmutes below ~5% duty, except sticky game mutes
which need a power cycle). Turns skip muted seats. So the live symptom is now
"one seat dead + muted in diag", not a chattering table — **check the diag muted
flag before bench-debugging.**

## Power: single-feed strip droop

The assembled octagon (221 LEDs) is powered from a **single feed point** through
the dev board's `5V` pin. Confirmed 2026-07-20: all-white is uniform at ~0.5 A
but fades warm/dim from 1.5 A up — the strip's copper, not the connections.

As of 2026-08-16 the table runs off a USB wall adapter (was a powerbank), and
**the wiring is set as it stands** — mid/end injection and the split-cable
rewire are deferred indefinitely.

**Do not raise `MAX_POWER_MA` because the supply improved.** Two independent
limits, neither of which is the power source:

1. `MAX_POWER_MA = 1500` in `octagon_core.h` is sized for the **dev board's own
   USB connector and 5 V trace** (~1.5–2 A continuous). 1500 mA LEDs + ~250 mA
   ESP ≈ 1.75 A. A beefier supply doesn't widen that trace.
2. Copper droop above ~1 A needs three injection points (start, mid corner
   between sides 4–5, end).

The cap only ever dims the three all-on moments (setup blink, READY all-on,
ready-flash). The lever that actually brightens ordinary play is the **runtime
brightness percentage**, default 50%; one side at 100% is ~1.0–1.4 A and stays
inside the cap. Raising the cap to 2500 requires splitting 5 V at the source so
LED current bypasses the board, *plus* the injection points.

The board cannot detect its power source — nothing distinguishes wall from
battery at any GPIO. GPIO 10 is the one free ADC1 pin (piezos use 1, 2, 4–9;
GPIO 3 is a JTAG strap) and would take a rail-sense divider if closed-loop
backoff is ever wanted.
```

- [ ] **Step 8: Write `$BENCH/bench/conventions.md`**

```markdown
# Bench conventions

## Project layout

Every project under `projects/` is **self-contained**. It owns its `Makefile`,
`.venv`, `requirements.txt`, `firmware/`, `scripts/`, `tests/`, `doc-src/`, and
its own `docs/superpowers/{specs,plans}/`. Nothing at the bench root is required
for a project to build.

That is deliberate: the tooling is validated against real hardware, and a
project that depends on root-level state can't be verified in isolation or
lifted back out.

## What belongs at the bench root

Only what is genuinely shared across builds: the parts inventory, bench lore,
these conventions, and the adding-a-project procedure. When in doubt, it goes in
the project.

## Paths and cwd

Bench scripts are cwd-sensitive by design — they write next to the project, not
next to themselves. Always drive them through make:

    make test                     # default project
    make PROJ=<name> test
    make -C projects/<name> test

Running a script by path from the bench root will scatter its output into the
monorepo root, outside the reach of the project's `.gitignore`.

## History

Projects are brought in with `git subtree`, so their commits are preserved.
One consequence to know: **`git log --follow` returns nothing** for files inside
a subtree, and `git log -- projects/<name>/<file>` shows only the merge.
`git blame` still works. To read a file's real history, use the pre-merge tip
recorded in `adding-a-project.md`:

    git log <pre-merge-tip> -- firmware/<file>

## Secrets

Credentials live in a gitignored `secrets.h` (or equivalent) inside the project,
never in a tracked file. The tracked `secrets.example.h` documents the shape.
Because these files are ignored, **they are not carried by any git operation** —
they must be hand-copied when a project moves, and they are the first thing to
check when a migrated project's radio comes up dead.
```

- [ ] **Step 9: Write `$BENCH/bench/adding-a-project.md`**

Every trap this migration hit, as a named step.

```markdown
# Adding a project to the bench

Brings an existing repo in under `projects/<name>/` with its history intact.
Each step exists because skipping it caused a real failure during the
octagonal-led-turn-counter migration.

## 1. Push the source first

    git -C <source> push origin main
    git -C <source> rev-list --left-right --count origin/main...main   # must be 0 0

Not optional. It is the pre-move backup, and it forces you to notice unpushed
work.

## 2. Subtree from the LOCAL path, never the remote URL

`git subtree add <url> main` is a plain fetch of whatever that repository has.
If local `main` is ahead of `origin/main`, the difference is **silently
dropped** — during the turn counter migration this would have imported 60 of 66
commits, losing an entire feature with no error.

    git remote add <name>-src /absolute/path/to/source
    git fetch <name>-src
    git log --oneline <name>-src/main | wc -l     # assert the expected count

## 3. Never pass `--squash`

`--squash` replaces the source tip with a synthetic parentless commit; the real
commits become unreachable. That is a fresh import wearing a subtree's clothes.

    git subtree add --prefix=projects/<name> <name>-src/main

The prefix must **not** already exist — subtree refuses otherwise. So this comes
before any hand-copying.

    git remote remove <name>-src

## 4. Hand-copy what git carries none of

`git subtree` moves tracked content only. Everything gitignored stays behind and
dies with the old checkout. Enumerate it first:

    git -C <source> status --ignored --short

Typical keepers: `secrets.h` (credentials — often exist nowhere else on disk),
recorded measurement data, `.claude/settings.local.json`. Typical discards:
`.venv`, `build/`, `__pycache__`, `.pytest_cache`.

Verify afterwards that a copied secret is still ignored at its new depth:

    git check-ignore -v projects/<name>/path/to/secrets.h   # must resolve
    git status --porcelain                                   # must stay empty

## 5. Recreate the venv — never copy it

A copied venv's interpreter survives relocation, but every console script in
`.venv/bin/` carries a shebang hard-coded to the old absolute path and becomes
`bad interpreter` once the old directory is gone.

    cd projects/<name>
    python3 -m venv .venv && .venv/bin/python3 -m pip install -r requirements.txt

## 6. Record what the merge does not

A subtree merge records the split sha but **never the source URL**. Add a row
here so provenance survives:

| Project | Source | Pre-merge tip | Imported |
|---|---|---|---|
| octagonal-led-turn-counter | `github.com/tucksravin/octagonal-led-turn-counter` (archived) | *(record at migration)* | 2026-09 |

## 7. Update the shared layer

Add the project to the root `README.md` and `CLAUDE.md` tables. Fold any
reusable parts into `inventory.md`, and any hard-won failure signatures into
`bench/lore.md`.
```

- [ ] **Step 10: Commit the shared layer**

```bash
cd $BENCH
git add -A
git commit -m "bench: root scaffold — shared layer for workshop projects

CLAUDE.md carries the superpowers redirect (specs live with their project),
the board-touching-commands rule, and the cwd convention. bench/lore.md is the
durable copy of bench facts that otherwise live only in path-keyed auto-memory.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 11: Verify the preconditions `git subtree` requires**

```bash
git status --porcelain ; echo "[clean above]"
test -e projects/octagonal-led-turn-counter && echo "PREFIX EXISTS — STOP" || echo "prefix free"
```

Expected: nothing before `[clean above]`, then `prefix free`. `git subtree` refuses a dirty tree and refuses an existing prefix.

---

## Task 5: Subtree import

**Files:** creates `$PROJ/` with the full project tree.

- [ ] **Step 1: Add the source as a local remote and fetch**

```bash
cd $BENCH
git remote add octagon $OLD
git fetch octagon
```

- [ ] **Step 2: Assert the commit count before importing**

```bash
git log --oneline octagon/main | wc -l
```

Expected: **exactly** the `commits` value recorded in Task 2 Step 2. Compare directly rather than against a number written here — every commit added between now and the migration shifts it:

```bash
test "$(git log --oneline octagon/main | wc -l)" = "$(git -C $OLD rev-list --count main)" \
  && echo "  counts match" || echo "  MISMATCH — STOP"
```

**If this number is lower than your baseline, stop.** It means the fetch found a stale ref, and the import would silently lose commits.

- [ ] **Step 3: Import, with no squash**

```bash
git subtree add --prefix=projects/octagonal-led-turn-counter octagon/main
```

Expected: `Added dir 'projects/octagonal-led-turn-counter'`.

- [ ] **Step 4: Verify the import — all gates must pass**

```bash
cd $BENCH
echo "subtree tree == source root tree?"
test "$(git rev-parse HEAD:projects/octagonal-led-turn-counter)" = "$(git -C $OLD rev-parse main^{tree})" && echo "  YES" || echo "  NO — STOP"

echo "root commit reachable?"
git cat-file -t 566366eb62cf370e71340c20dbfcdd5dc03e04dd

echo "source tip reachable?"
git cat-file -t $(git -C $OLD rev-parse main)

echo "tracked file count matches source?"
test "$(git ls-tree -r HEAD projects/octagonal-led-turn-counter | wc -l)" = "$(git -C $OLD ls-files | wc -l)" && echo "  YES" || echo "  NO — STOP"

echo "merge trailers:"
git log -1 --format=%B | grep -E 'git-subtree-(dir|split)'

echo "absolute paths committed: $(git grep -c 'Documents/GitHub' -- projects/ | wc -l | tr -d ' ')"
```

Expected: `YES`; both `cat-file` calls print `commit`; tracked-file count matches the Task 2 baseline (79 after Task 1 removes the stray PNG); both `git-subtree-dir:` and `git-subtree-split:` present; absolute-path count `0`.

- [ ] **Step 5: Drop the temporary remote**

```bash
git remote remove octagon
git remote -v
```

Expected: no output.

---

## Task 6: Hand-copy the untracked keepers

**Files:**
- Create: `$PROJ/firmware/turn_counter/secrets.h`
- Create: `$PROJ/data/piezo/*.csv`
- Create: `$PROJ/doc-src/table_qr.svg`, `$PROJ/table_qr.pdf`
- Create: `$BENCH/.claude/settings.local.json`

**Must come after Task 5** — the prefix must not exist when subtree runs.

- [ ] **Step 1: Copy the credentials file**

Exists nowhere else on disk. Without it the firmware compiles fine but comes up with the radio off, which presents as "the migration broke the firmware."

```bash
cp $OLD/firmware/turn_counter/secrets.h $PROJ/firmware/turn_counter/secrets.h
diff -q $OLD/firmware/turn_counter/secrets.h $PROJ/firmware/turn_counter/secrets.h && echo "identical"
```

Expected: `identical`.

- [ ] **Step 2: Copy the piezo recordings**

`octagon_core.cpp:32` cites these by path as the derivation for `TAP_DELTA = 720`. Not regenerable without re-instrumenting the assembled table.

```bash
mkdir -p $PROJ/data
cp -R $OLD/data/piezo $PROJ/data/
diff -r $OLD/data $PROJ/data && echo "identical"
ls -1 $PROJ/data/piezo | wc -l
```

Expected: `identical`, then `4`.

- [ ] **Step 3: Copy the QR artifacts**

Regenerable via `make qr URL=…`, but free to carry.

```bash
cp $OLD/doc-src/table_qr.svg $PROJ/doc-src/ 2>/dev/null || echo "absent, will regenerate"
cp $OLD/table_qr.pdf $PROJ/ 2>/dev/null || echo "absent, will regenerate"
```

- [ ] **Step 4: Write the bench-root permission rules**

Under bench-root cwd this belongs at the **bench root**, not the project — that is the workspace root Claude reads from. Two rules need path changes; the other 16 encode no paths.

Create `$BENCH/.claude/settings.local.json`:

```json
{
  "permissions": {
    "allow": [
      "Bash(git log:*)",
      "Bash(git status:*)",
      "Bash(git stash list:*)",
      "Bash(git branch:*)",
      "Bash(git worktree list:*)",
      "Bash(git grep:*)",
      "Bash(git diff:*)",
      "Bash(git show:*)",
      "Bash(ls:*)",
      "Bash(find:*)",
      "Bash(wc:*)",
      "Bash(grep:*)",
      "Bash(rg:*)",
      "Bash(make compile-all:*)",
      "Bash(make -C projects/octagonal-led-turn-counter:*)",
      "Bash(arduino-cli:*)",
      "Bash(qlmanage:*)",
      "Bash(cp:*)",
      "Write(projects/octagonal-led-turn-counter/docs/morning-reports/**)"
    ],
    "deny": []
  }
}
```

Changes from the original 18: `Write(docs/morning-reports/**)` re-rooted, and `Bash(make -C projects/octagonal-led-turn-counter:*)` added. `Bash(make compile-all:*)` is **kept** — the root Makefile's `PROJ` default means `make compile-all` still works verbatim from the bench root.

- [ ] **Step 5: Verify the secret is still invisible to git at its new depth**

This is the check that matters most — a wrong answer publishes Wi-Fi credentials to a public repo.

```bash
cd $BENCH
git check-ignore -v projects/octagonal-led-turn-counter/firmware/turn_counter/secrets.h
git status --porcelain ; echo "[clean above]"
```

Expected: the ignore resolves to **the project's own** `.gitignore` with the rule `firmware/*/secrets.h` — i.e. a line reading `projects/octagonal-led-turn-counter/.gitignore:<N>:firmware/*/secrets.h`. Don't check `<N>` against a number written here: Task 1 Step 4 deletes a line above it, shifting the rule from 13 to 12. What matters is that the path resolves to the **project** `.gitignore`, not the bench root's.

And **nothing** appears before `[clean above]`.

**If `secrets.h`, `data/`, or `settings.local.json` appears in `git status`, stop and fix the ignore rules before any commit or push.**

---

## Task 7: Recreate the venv and verify the tooling

**Files:** creates `$PROJ/.venv/` (gitignored).

- [ ] **Step 1: Create the venv fresh — never copy it**

A copied venv's 15 console scripts carry shebangs hard-coded to the old absolute path.

```bash
cd $PROJ
python3 -m venv .venv
.venv/bin/python3 -m pip install --quiet -r requirements.txt
.venv/bin/python3 -c "import weasyprint, serial, segno, markdown, pypdf; print('imports ok')"
```

Expected: `imports ok`. The PDF toolchain is exactly pinned, so this reproduces the document build.

- [ ] **Step 2: Run the test suite through the delegator**

```bash
cd $BENCH
make test
```

Expected: `53 passed`. This also proves the root Makefile's `$(MAKE) -C` delegation works.

- [ ] **Step 3: Compile every sketch**

Laptop-only — the toolchain never touches the board.

```bash
cd $BENCH
make compile-all
```

Expected: each sketch compiles, ending `All sketches compiled.` This proves `--libraries firmware/libraries` still resolves relative to the project.

- [ ] **Step 4: Probe PDF determinism before it matters**

Task 8 must rebuild the PDFs. Find out now whether an unmodified rebuild churns them.

```bash
cd $BENCH
make pdf
git status --porcelain projects/octagonal-led-turn-counter
```

Expected, ideally: no output — WeasyPrint is deterministic and the tracked PDFs are unchanged.

If PDFs show as modified, WeasyPrint is not byte-deterministic here. Restore and note it:

```bash
git checkout -- 'projects/octagonal-led-turn-counter/*.pdf'
```

Task 8's commit will then include unavoidable binary churn — expected, not a failure.

---

## Task 8: Promote `inventory.md` and the spec, fix the links

**Files:**
- Move: `$PROJ/inventory.md` → `$BENCH/inventory.md`
- Move: `$PROJ/docs/superpowers/specs/2026-09-04-the-bench-monorepo-design.md` → `$BENCH/docs/superpowers/specs/`
- Move: `$PROJ/docs/superpowers/plans/2026-09-04-the-bench-monorepo.md` → `$BENCH/docs/superpowers/plans/`
- Modify: `$PROJ/design_doc_simple.md:133`, `$PROJ/shopping_list.md:5`, `$PROJ/README.md`

- [ ] **Step 1: Move the inventory to the bench root**

It exists to track surplus *across* projects; scoped to one it cannot do that job.

```bash
cd $BENCH
git mv projects/octagonal-led-turn-counter/inventory.md inventory.md
```

- [ ] **Step 2: Move this migration's spec and plan to the bench tier**

They describe the monorepo itself, not the turn counter — the one case the bench-root `docs/superpowers/` exists for.

```bash
mkdir -p docs/superpowers/specs docs/superpowers/plans
git mv projects/octagonal-led-turn-counter/docs/superpowers/specs/2026-09-04-the-bench-monorepo-design.md docs/superpowers/specs/
git mv projects/octagonal-led-turn-counter/docs/superpowers/plans/2026-09-04-the-bench-monorepo.md docs/superpowers/plans/
```

- [ ] **Step 3: Find every reference to the moved inventory**

```bash
cd $BENCH
git grep -n "inventory.md" -- projects/
```

Expected hits: `design_doc_simple.md`, `shopping_list.md`, `README.md`.

- [ ] **Step 4: Re-point the links**

From inside the project, the bench root is two levels up:

```bash
cd $PROJ
sed -i '' 's|(inventory\.md)|(../../inventory.md)|g' design_doc_simple.md shopping_list.md README.md
grep -n "inventory.md" design_doc_simple.md shopping_list.md README.md
```

Expected: every hit now reads `../../inventory.md`.

- [ ] **Step 5: Update the inventory's own header for bench scope**

Open `$BENCH/inventory.md` and revise the opening paragraph: it currently says "parts bought for this project." It is now the bench-wide record. Change the framing to cover every project, and re-point its own link to the shopping list, which stayed behind:

```bash
cd $BENCH
sed -i '' 's|(shopping_list\.md)|(projects/octagonal-led-turn-counter/shopping_list.md)|g' inventory.md
grep -n "shopping_list.md" inventory.md
```

Expected: the link now points into the project.

- [ ] **Step 6: Fix the project README's stale tree**

`README.md:9-41` lists 4 sketches; `firmware/` holds 8 plus `libraries/`. It also says "From the repo root," which is now ambiguous. Update the tree to match reality, drop the `inventory.md` row (it moved), and change "repo root" to "the project directory."

- [ ] **Step 7: Rebuild the PDFs so the shipped artifacts match**

`design_doc_simple.pdf` has the old inventory link baked in.

```bash
cd $BENCH && make pdf
```

- [ ] **Step 8: Verify no stale links remain**

```bash
cd $BENCH
git grep -n "](inventory.md)" -- projects/ ; echo "[no bare links above]"
make test
```

Expected: nothing before `[no bare links above]`, then `53 passed`.

- [ ] **Step 9: Commit**

```bash
cd $BENCH
git add -A
git commit -m "bench: promote inventory.md to the root, re-point its links

inventory.md tracks surplus across projects — bulk resistors, the JST kit,
leftover strip — so it belongs at the bench root, not inside one project.
Links in design_doc_simple.md, shopping_list.md and the project README
re-pointed; PDFs rebuilt so the shipped artifacts match.

The migration's own spec and plan move to the bench docs/superpowers/ tier,
which exists for cross-project work.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 9: Carry over Claude Code and editor state

**Files:** creates `$NEWSLUG/` (outside the repo).

- [ ] **Step 1: Copy the auto-memory to the new path key**

Memory is keyed by cwd path with `/` → `-`. Both candidate new paths resolve to an empty memory. **Copy, don't move** — the old path must keep working until Task 12.

```bash
cp -a $OLDSLUG $NEWSLUG
ls $NEWSLUG/memory/
```

Expected: `MEMORY.md` plus the 8 memory files.

- [ ] **Step 2: Correct the memory entries the migration invalidates**

Two files carry claims that are now stale:

- `maintain-parts-inventory.md` says inventory lives at "repo root" — it is now the **bench root**, one level above the project, and it is bench-wide rather than project-scoped.
- `hardware-phase-no-console.md` references `make flash-turn` etc. without the new delegator forms.

Edit both in `$NEWSLUG/memory/` to match the new layout, and add a line to `MEMORY.md` pointing at `bench/lore.md` as the committed, durable copy.

- [ ] **Step 3: Verify the seeded memory is actually read**

The whole remedy for the largest silent loss rests on this working. Start a session at `$BENCH` and ask: *"which serial port is the board?"*

Expected: a correct answer naming `/dev/cu.usbserial-*` and noting the `usbmodem` ports are the LG monitor.

If it fails, the memory copy didn't take — but `bench/lore.md` is committed and readable either way, which is why it was written first.

- [ ] **Step 4: Point VS Code at the new root**

```bash
code $BENCH
```

Open `projects/octagonal-led-turn-counter/firmware/turn_counter/turn_counter.ino` and confirm `octagon_core.h` resolves. Expect a workspace-trust prompt — the new path has never been trusted.

If IntelliSense is broken, regenerate it from inside the project:

```bash
cd $PROJ && python3 scripts/gen_intellisense.py
```

(This is the one Make target that uses the bare system `python3`, not the venv.)

---

## Task 10: Publish `the-bench`

- [ ] **Step 1: Create the public repo and push**

```bash
cd $BENCH
gh repo create the-bench --public --source=. --remote=origin --push
```

- [ ] **Step 2: Confirm no secrets went up**

```bash
git ls-files | grep -E 'secrets\.h$|\.env' ; echo "[none above]"
gh repo view --json visibility,name
```

Expected: nothing before `[none above]`; visibility `PUBLIC`.

**If `secrets.h` appears in `git ls-files`, the push leaked credentials.** Rotate the Wi-Fi and OTA passwords immediately and rewrite the pushed history.

---

## Task 11: Board gates — **the user runs these at the bench**

Nothing above proves `secrets.h` landed intact. Only the board can. **Do not proceed to Task 12 until these pass.**

- [ ] **Step 1: Confirm the board answers**

```bash
cd $BENCH && make ping
```

Expected: the board resets and serial output appears. This also proves the `/dev/cu.usbserial-*` port glob still resolves through the delegator.

- [ ] **Step 2: Push firmware over the air — the real `secrets.h` test**

```bash
cd $BENCH && make ota
```

Expected: compile succeeds under the `min_spiffs` partition, then the OTA upload completes.

`ota_flash.py:24` reads `firmware/turn_counter/secrets.h` and exits at `:78-82` if absent — so a clean OTA is proof the file arrived intact.

- [ ] **Step 3: If OTA fails, fall back to USB**

```bash
cd $BENCH && make flash-turn
```

Then re-run Step 1. A USB flash bypasses the OTA password entirely, so success here with OTA failing points at `secrets.h` content rather than the migration.

- [ ] **Step 4: Confirm normal play**

Tap a seat; the lit zone should advance clockwise. Check `/api/diag` for unexpected `"muted":1` rows.

---

## Task 12: Delete the old checkout and archive the source

**Only after Task 11 passes.** Until then, the wrapper is the rollback.

- [ ] **Step 1: Re-copy anything written during the board gates**

`make record` or a bench session may have added files since Task 6.

```bash
cp $OLD/firmware/turn_counter/secrets.h $PROJ/firmware/turn_counter/secrets.h
cp -R $OLD/data/piezo/. $PROJ/data/piezo/
diff -r $OLD/data $PROJ/data && echo "identical"
cd $BENCH && git status --porcelain ; echo "[clean above]"
```

Expected: `identical`, and nothing before `[clean above]`.

- [ ] **Step 2: Final pre-deletion audit**

```bash
comm -23 \
  <(cd $OLD && git status --ignored --short | awk '{print $2}' | sort) \
  <(cd $PROJ && git status --ignored --short | awk '{print $2}' | sort)
echo "[anything above exists only in the old tree]"
```

Review each line. Expected to appear and be fine to lose: `.venv/`, `build/`, `firmware/turn_counter/build/`, `__pycache__/`, `.pytest_cache/`, `.DS_Store`.

**Anything else on that list must be copied before proceeding.**

- [ ] **Step 3: Delete the wrapper**

The wrapper is not itself a git repo (verified), and `erp-industrial` was removed in Task 3, so nothing is stranded.

```bash
rm -rf $WRAP
ls /Users/tuckerlemos/Documents/GitHub | grep octagonal ; echo "[gone above]"
```

Expected: nothing before `[gone above]`.

- [ ] **Step 4: Remove the orphaned memory slug**

```bash
rm -rf $OLDSLUG
ls ~/.claude/projects/ | grep -c the-bench
```

Expected: `1`.

- [ ] **Step 5: Archive the source repo**

```bash
gh repo archive tucksravin/octagonal-led-turn-counter --yes
gh repo view tucksravin/octagonal-led-turn-counter --json isArchived
```

Expected: `{"isArchived":true}`. It stays as provenance; `the-bench` is now the live repo.

- [ ] **Step 6: Record provenance in the procedure doc**

The subtree merge recorded the split sha but never the source URL. Fill in the table in `$BENCH/bench/adding-a-project.md` §6 with the pre-merge tip from Task 2 Step 2 and the archived URL, then commit:

```bash
cd $BENCH
git add bench/adding-a-project.md
git commit -m "bench: record the turn counter's provenance — source URL and pre-merge tip

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push
```

- [ ] **Step 7: Retire the backup**

Keep `~/Desktop/octagonal-led-turn-counter-premove-*.tar.gz` for a week of normal use, then delete.

---

## Rollback

Before Task 12 Step 3, rollback is complete and cheap: `rm -rf $BENCH`, and the old checkout is untouched and still works.

After Task 12 Step 3, rollback is the dated tarball from Task 2 Step 4 plus the archived GitHub repo.

## Self-review notes

Spec sections mapped to tasks: §3 layout → Tasks 4, 8. §4.1 erp-industrial → Task 3. §4.2 local-path subtree → Task 5 Steps 1-2. §4.3 gitignore semantics → Task 6 Step 5. §4.4 venv → Task 7 Step 1. §4.5 dead credential → Task 1 Step 1. §5 untracked keepers → Task 6. §6.1 superpowers redirect → Task 4 Step 2. §6.2 permissions → Task 6 Step 4. §6.3 cwd-sensitivity → Task 4 Steps 2-3. §6.4 memory slug → Task 9. §7 shared layer → Task 4. §8 verification → Tasks 5 Step 4, 7, 11. §9 accepted costs → documented in `conventions.md` (Task 4 Step 8). D9 cleanups → Task 1.
