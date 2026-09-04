# the-bench — workshop monorepo

**Status:** design approved 2026-09-04, implementation pending
**Scope:** create `the-bench`, migrate `octagonal-led-turn-counter` into it with history, establish the shared bench layer

---

## 1. Why

Bench work has outgrown one repo. Parts bought for the turn counter are surplus that future
builds should draw on; the bench lore (which serial port is the board, why chaotic taps meant a
floating ADC pin, the `min_spiffs` partition requirement) applies to any ESP32 build, not just
this one. Today that context lives in two places that don't survive: a single project's
`inventory.md`, and Claude's per-project auto-memory, which is keyed to an absolute path and
evaporates the moment the directory moves.

`the-bench` is a monorepo whose root holds the shared workshop context and whose `projects/`
subdirectories hold self-contained builds.

**In scope:** physical/bench builds only. Client web work stays out. Personal software that
isn't bench work (games, songbook, the VS Code extension) stays out.

**Migrating now:** `octagonal-led-turn-counter` only. `scriptorium-setup` is the expected second
occupant and joins later via the documented procedure — it is not part of this work.

## 2. Decisions

| # | Decision | Rationale |
|---|----------|-----------|
| D1 | Repo at `~/Documents/GitHub/the-bench` | Sibling to existing work; no new roots to remember |
| D2 | Project lands at `projects/octagonal-led-turn-counter/` | Leaves room for peers without renaming anything later |
| D3 | History preserved via `git subtree`, **no `--squash`** | `--squash` replaces the real tip with a synthetic parentless commit; the 66 commits become unreachable. That is the "fresh import" this design rules out. |
| D4 | Tooling stays **per-project**; the project's `Makefile`, `.venv`, `requirements.txt` are untouched | The Makefile is validated against real hardware and cannot be re-verified without the board. Not rewriting it is the safe call. Verified: `make -C` preserves every target. |
| D5 | Bench root gets a **thin delegating Makefile** using `$(MAKE) -C` | See §7.3 — `-f` is a silent trap |
| D6 | Superpowers specs/plans stay **per-project**, redirected by a bench-root `CLAUDE.md` | Specs belong beside the code they describe. `writing-plans/SKILL.md:19` explicitly sanctions a user override of the default location. |
| D7 | `the-bench` is **public**; `tucksravin/octagonal-led-turn-counter` is **archived** read-only | Keeps the build log shareable; archiving preserves provenance without maintaining two remotes |
| D8 | Canonical session cwd is the **bench root** | One session sees every project and the shared layer. Consequences in §6. |
| D9 | Pre-existing cleanups ride along as a **separate commit** | Prescribed by `MORNING_REPORT_2026-07-14.md:7`; kept out of the migration diff |
| D10 | The stray `erp-industrial` fork is **deleted, not relocated** | See §4.1 |
| D11 | `data/piezo/` stays gitignored | Considered and declined; it remains on the hand-copy list in §5 |

## 3. Target layout

```
the-bench/
├── CLAUDE.md                    NEW   session rules: superpowers redirect, board rule, project router
├── README.md                    NEW   what the bench is + project index
├── Makefile                     NEW   thin delegator to projects/<name>
├── .gitignore                   NEW   OS noise + cwd-mistake insurance (§7.4)
├── .gitattributes               NEW   `* text=auto`, tree-wide
├── .claude/settings.local.json  NEW, untracked — permission rules rewritten for bench-root cwd (§6.2)
├── inventory.md                 MOVED UP from the project root
├── docs/superpowers/specs/      this document, promoted from the project during migration
├── bench/
│   ├── lore.md                  NEW   hard-won bench facts, committed (§7.2)
│   ├── conventions.md           NEW   how a bench project is laid out
│   └── adding-a-project.md      NEW   the subtree procedure for the next project
└── projects/
    └── octagonal-led-turn-counter/     subtree'd, contents 100% unchanged
```

`inventory.md` moves up because it exists to track *surplus across projects* — bulk resistors,
the JST kit, leftover WS2812B strip. Scoped to one project it can't do that job.

Everything inside `projects/octagonal-led-turn-counter/` is byte-identical to today.

## 4. Corrections to naive assumptions

Each of these was verified against the filesystem; each would have caused real damage.

### 4.1 `erp-industrial` must be deleted, not moved

A stray clone of `erp-industrial` sits in the wrapper folder. The obvious move — relocate it to
`~/Documents/GitHub/erp-industrial` — is wrong: **that path is already occupied by a different,
live repository**, the Reddoor org clone (`reddoorla/erp-industrial`, HEAD `fa5c05c`) complete
with `.env`, `.env.bak`, and `node_modules`. `mv` onto an existing directory nests inside it,
depositing a stale fork into a live client site, unignored.

The wrapper copy is a personal fork (`tucksravin/erp-industrial`, HEAD `3947081`): 98 commits
behind its own origin, clean tree, empty stash, nothing unpushed, zero untracked or ignored
files. `git merge-base --is-ancestor 3947081 main` succeeds inside the live repo — **the stray's
tip is already an ancestor of the live main. It holds no unique objects.** Delete it.

### 4.2 The subtree source must be the local path

`origin/main` is 60 commits; local `main` is 66. The six unpushed commits are the **entire tap
guard v2 feature** (`7a26f3d` spec+plan, `2a95d19` test, `b063956` octagon_core, `908cc6d`
web_ui, `6075c53` docs, `bee475c` tap guard v2). `git subtree add <url> main` is a plain fetch of
whatever the named repository has, so pointing it at GitHub imports 60 commits and lands a tree
with all tap-guard work missing — silently, with no error.

Push first (as a pre-move backup), then subtree from the **local absolute path**, and assert the
count is 66 before proceeding.

### 4.3 Gitignore patterns survive the depth change

Checked because getting it wrong would publish live Wi-Fi credentials to a public repo.

Patterns **containing a slash** are relative to the directory of their own `.gitignore`. Once the
file lives at `projects/octagonal-led-turn-counter/.gitignore`, `firmware/*/secrets.h`,
`doc-src/table_qr.svg`, and the `doc-src/_build_*.html` entries anchor correctly to the project
subtree. Patterns **without** a slash (`.venv/`, `data/`, `build/`, `__pycache__/`, `*.pyc`,
`.pytest_cache/`, `.DS_Store`, `table_qr.pdf`) match at any depth below that directory — already
how they behave today.

All 12 patterns remain correct. Move `.gitignore` with the tree unchanged; do **not** merge it
into a bench-root `.gitignore`.

Independently confirmed: `secrets.h` has **never** been tracked on any ref
(`git log --all --full-history -- '*secrets.h'` is empty; an object-name scan finds only
`secrets.example.h`). History carries none of the live credentials.

### 4.4 The `.venv` is recreated, never copied

The venv's interpreter survives relocation, but all 15 console scripts in `.venv/bin/`
(`pytest`, `pip`, `weasyprint`, `segno`, `markdown_py`, …) carry a shebang hard-coded to the old
absolute path, as does `bin/activate`. They appear to work only while the old directory still
exists; after deletion they become `bad interpreter: No such file or directory`.

Recreating is also cheaper — the venv is 81 MB — and safe: `requirements.txt` pins the entire PDF
toolchain exactly (`markdown==3.9`, `pyserial==3.5`, `weasyprint==66.0`, `pypdf==6.10.2`), with
only `pytest~=8.4` and `segno~=1.6` floating. A rebuild reproduces the document build.

The Makefile is unaffected either way — every target invokes `.venv/bin/python3 <script>` or
`-m pytest`, never a console script. The exception is `make vscode`, which uses the bare system
`python3` and touches the venv not at all.

### 4.5 A dead OTA password is in public history

`tucksravin/octagonal-led-turn-counter` is public. The retired OTA password `letsplayagame` is
reachable in history (`fb9aff5`) and still present as a fixture in `tests/test_ota_flash.py` and
in `docs/superpowers/plans/2026-08-14-piezo-map-and-ota.md`.

It is **dead**: the current firmware defaults `OTA_PASSWORD ""` at `turn_counter.ino:25` and
reads the real value from the gitignored `secrets.h` — the fix `MORNING_REPORT_2026-07-14.md:22`
prescribed. No live credential is in git.

Under D7 the subtree carries that history into a public `the-bench`. Accepted, with one change:
**swap the test fixture to an obviously fake value** so the string stops reading as a real
credential to anyone scanning the repo. History is not rewritten — a 66-commit rewrite is not
worth it for a retired LAN-only password.

## 5. What `git subtree` will not carry

Untracked files are invisible to git and live only in the folder this migration deletes. They
must be hand-copied **after** the subtree lands (the prefix must not exist beforehand) and
**before** any deletion.

| Path | Why it matters | Regenerable? |
|------|----------------|--------------|
| `firmware/turn_counter/secrets.h` | Wi-Fi credentials + the live OTA password. Exists nowhere else on disk. Without it the firmware compiles fine but comes up with the radio off — the failure presents as "the migration broke the firmware," and recovery is a USB reflash at the bench. | **No** |
| `data/piezo/*.csv` | 4 bench captures, 548 KB, from 2026-07-28. `octagon_core.cpp:32` cites them **by path** as the derivation for `TAP_DELTA = 720`. | **No** — requires re-instrumenting the assembled table |
| `.claude/settings.local.json` | 18 permission rules. Hidden by the *global* `**/.claude/` ignore, not the repo's. Under D8 its destination is the **bench root** (`the-bench/.claude/settings.local.json`), not the project — that is the workspace root Claude reads from — with the two path-bearing rules rewritten per §6.2. | No |
| `doc-src/table_qr.svg`, `table_qr.pdf` | QR sticker for the web UI | Yes — `make qr URL=…` |

**Deliberately not copied:** `.venv` (81 MB, §4.4), `build/` and `firmware/turn_counter/build/`
(~180 MB of arduino-cli output), `__pycache__`, `.pytest_cache`, `.DS_Store`.

## 6. Consequences of a bench-root session cwd (D8)

### 6.1 The superpowers redirect becomes load-bearing

`brainstorming/SKILL.md` and `writing-plans/SKILL.md` both write to a **repo-root-relative**
`docs/superpowers/{specs,plans}/`. A session at the bench root therefore writes the next turn
counter spec to `the-bench/docs/superpowers/` — one directory silently accumulating plans from
every project with no way to tell them apart. Nothing errors, so it would go unnoticed for
months.

The bench-root `CLAUDE.md` must carry the redirect, and it must be in the **first bench-root
commit**, before any superpowers skill runs in the new repo.

The bench root keeps a `docs/superpowers/` of its own for genuinely cross-project work — this
document lives there.

### 6.2 Permission rules need rewriting

The existing rules are written against today's invocation style and stop matching from the bench
root:

- `Bash(make compile-all:*)` — the command becomes `make -C projects/octagonal-led-turn-counter compile-all`, which no longer has `make compile-all` as a prefix. Rewrite to cover the `-C` form.
- `Write(docs/morning-reports/**)` — relative; the real target becomes `projects/octagonal-led-turn-counter/docs/morning-reports/**`.

The other 16 rules (git reads, `ls`, `find`, `grep`, `rg`, `wc`, `arduino-cli`, `qlmanage`, `cp`)
encode no paths and carry over unchanged.

### 6.3 Bench scripts are cwd-sensitive

Three scripts anchor on cwd rather than `__file__`: `make_qr.py` (writes `doc-src/table_qr.svg`
and `table_qr.pdf`), `record_piezos.py` (writes `data/piezo/<timestamp>.csv`, creating parents),
and `ota_flash.py` (reads `firmware/turn_counter/secrets.h`).

Driven through `make -C`, cwd is the project directory and all three behave exactly as today.
Hand-run by path from the bench root, the first two would silently create
`the-bench/doc-src/` and `the-bench/data/piezo/` — project artifacts polluting the monorepo root,
outside the reach of the project-scoped `.gitignore`. (`ota_flash.py` fails loudly instead, so it
is self-diagnosing.)

Mitigated two ways: a rule in `CLAUDE.md` — *always drive bench scripts via
`make -C projects/<name> <target>`, never by path from the root* — and the defensive bench-root
`.gitignore` in §7.4.

### 6.4 The memory slug

Auto-memory is keyed to the cwd path with `/` → `-`. Under D8 the new key is
`-Users-tuckerlemos-Documents-GitHub-the-bench`, which does not exist and resolves to an empty
memory. The existing directory is **copied** (not moved) to the new slug, and the copy is
repeated immediately before deletion to catch anything written in between.

Copying is the convenience; `bench/lore.md` (§7.2) is the durability — it is the copy that
survives a new machine.

## 7. Shared layer

### 7.1 `CLAUDE.md`
Must cover, at minimum:
- **Project router** — a table of `projects/*` and what each is; `ls projects/` is the live index
- **Superpowers redirect** — specs and plans go to `projects/<name>/docs/superpowers/`, citing `writing-plans/SKILL.md:19` so a session reads it as a sanctioned override rather than a conflict
- **The board rule**, verbatim from the clarified memory: don't run commands that touch hardware on the table (flash, monitor, ping, record, map-piezos, OTA, serial probes); git, file moves, `pytest`, `arduino-cli compile`, and `make pdf` are fine
- **`make -C` convention** and the cwd-sensitivity warning from §6.3
- **The inventory rule** — update `inventory.md` when parts are bought, consumed, or drawn from stock by a new project
- **The venv note** — after a fresh clone there is no `.venv`; `make test`/`make pdf` fail until it is created

### 7.2 `bench/lore.md`
The eight auto-memory facts, promoted to committed documentation: which serial port is the board
(and that the `usbmodem` ports are the LG monitor), the `min_spiffs` partition requirement for
`turn_counter`, that chaotic taps regardless of piezo meant a floating ADC pin rather than a
threshold problem, the serial workflow, the single-feed strip droop and the 700 mA cap pending
mid+end injection, and that board network reads need `ctx_execute` because the Bash sandbox
blocks LAN silently.

Written **before** the wrapper is deleted, while the memory directory is still reachable.

### 7.3 `Makefile` — delegation by directory
Must use `$(MAKE) -C projects/$(PROJ)`. **Never `make -f`.** The two are not interchangeable
here: `-f` expands to a byte-identical recipe in `-n` output, but then executes with cwd at the
bench root, where neither `.venv/` nor `tests/` exists. Every one of the ~20 targets has that
property, so a delegator written with `-f` breaks all of them at once while looking correct under
inspection.

If a match-anything rule (`%:`) is used, the root Makefile needs its own explicit `help` and
`.PHONY` so root-level targets aren't swallowed.

### 7.4 `.gitignore` at the bench root
Small and defensive: `.DS_Store` and `**/.DS_Store` (the project-scoped rule no longer covers
Finder files at the bench or `projects/` level), plus **unanchored** `build/`, `.venv/`,
`__pycache__/`, `.pytest_cache/`, `data/`, `table_qr.pdf` as insurance against the cwd mistakes in
§6.3.

Never copy the slash-bearing project rules up — they would anchor to the wrong directory.

### 7.5 `bench/adding-a-project.md`
The procedure for `scriptorium-setup` and everything after. Must include, as named steps, every
trap this migration hit: subtree from a local path with a commit-count assertion, never
`--squash`, hand-copy the untracked keepers, recreate the venv rather than copy it, and record the
source URL and tip sha (the subtree merge records the split sha but **never** the source URL).

## 8. Verification

Baselines, measured 2026-09-04 on a clean tree:

| Quantity | Value |
| --- | --- |
| Commits on `main` | 66 (0 merges) |
| Tip | `bee475c49e3ef70682f2550860c2b956184c608b` |
| Root tree | `d33e3ca64b715d99b0db2cf1ab3a9f626cac1338` |
| Root commit | `566366eb62cf370e71340c20dbfcdd5dc03e04dd` |
| Tracked files | 80 |
| `pytest tests/ -q` | 53 passed |
| `origin/main` | 60 commits — **6 behind** (§4.2) |

Also confirmed: the wrapper folder is not itself a git repo (safe to remove once both children
are handled), `~/Documents/GitHub/the-bench` is free, and `init.defaultBranch` is already `main`
on this machine — though `git init -b main` is used anyway to be explicit.

### Laptop-only gates

- `git log --oneline octagon/main | wc -l` = **66** before `git subtree add`
- After the add: `git rev-parse HEAD:projects/octagonal-led-turn-counter` = `d33e3ca6…`; `git cat-file -t` on both `bee475c4…` and `566366eb…` returns `commit`; 80 tracked files under the prefix; the merge message carries `git-subtree-dir:` and `git-subtree-split:`
- `git grep 'Documents/GitHub'` = 0 hits — no absolute paths committed
- `git check-ignore -v projects/…/firmware/turn_counter/secrets.h` resolves to the **project's** `.gitignore`, and `git status --porcelain` stays empty after the hand-copy
- `diff -r` on `data/` — identical, 4 files
- `make -C projects/octagonal-led-turn-counter test` → **53 passed**
- `make -C projects/octagonal-led-turn-counter compile-all` → every sketch compiles (proves `--libraries firmware/libraries` still resolves)
- `make -C projects/octagonal-led-turn-counter pdf` on unmodified sources, then `git diff --stat` — a determinism probe. If WeasyPrint output is non-deterministic, all 5 tracked PDFs churn and the inventory-link commit becomes a large binary diff. Restore with `git checkout` if so.
- Memory: start a session at the bench root and ask which serial port is the board. A correct `usbserial-*` answer proves the seeded memory is read.

### Board gates — the user runs these
Nothing above proves `secrets.h` landed intact; only the board can.
- `make ping` — the board answers and the port glob still works
- `make ota` — **the only proof `secrets.h` is correct** (`ota_flash.py:24`, `:78-82`)
- On OTA failure: `make flash-turn` over USB, then re-verify

**The wrapper folder is the rollback and is not deleted until the board gates pass.** The
migration therefore cannot complete in one laptop-only sitting.

### Preconditions
Before starting: close the VS Code window on the old path, close any `arduino-cli monitor` (it
holds the serial port exclusively), and end any Claude session whose cwd is the old path.
Otherwise memory written after the copy is silently lost at deletion.

A dated backup of the entire wrapper is taken outside `~/Documents/GitHub` before any change and
retired a week after the board gates pass.

## 9. Known costs, accepted

- **`git log --follow` returns nothing** for files inside the subtree, and `git log -- projects/…/<file>` shows only the merge. `git blame` still works. Workaround for `bench/conventions.md`: a dual pathspec, or `git log bee475c -- firmware/<file>` against the pre-merge sha.
- **The dead `letsplayagame` string** remains in public history under D7 (§4.5).
- **Two `.DS_Store` files** and other Finder noise at the wrapper level are discarded, not migrated.

## 10. Out of scope

- Migrating `scriptorium-setup` — deferred; `bench/adding-a-project.md` is the deliverable that enables it
- Any change to firmware behavior, the Makefile's contents, or the test suite
- Rewriting git history to purge the retired credential
- Tracking `data/piezo/` (D11)
- A shared bench-wide venv or root-level Python config — the per-project decision (D4) stands, and a bench-root `pyproject.toml` would silently capture this project's pytest rootdir
