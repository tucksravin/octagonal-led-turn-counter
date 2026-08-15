# Bench shortcuts. Port is auto-detected from the board's CP2102 UART bridge;
# override with e.g.  make monitor PORT=/dev/cu.usbserial-1130

PORT ?= $(firstword $(wildcard /dev/cu.usbserial-*))
BAUD ?= 115200
FQBN := esp32:esp32:esp32s3
# turn_counter overflows the default 1.25 MB app partition (Wi-Fi + OTA + mDNS),
# so it compiles/uploads with the min_spiffs scheme (1.9 MB app, keeps OTA).
FQBN_TURN := $(FQBN):PartitionScheme=min_spiffs
# turn_counter and eight share firmware/libraries/octagon_core (table geometry,
# calibrated side table, piezo tap detection). If your arduino-cli rejects
# --libraries, the single-library form is: --library firmware/libraries/octagon_core
LIBS := --libraries firmware/libraries
# Compile chatter: make flash-tap V=1 → full toolchain commands + all compiler
# warnings (arduino-cli hides both by default).
VERBOSE := $(if $(V),--verbose --warnings all,)
# compile-all is the correctness gate, so warnings are always on there — but not
# --verbose, which buries the one line that matters (the size summary) under
# every toolchain invocation and every cached-object path. V=1 adds it back.
CHECK := $(if $(V),--verbose,) --warnings all

# Every sketch, grouped by what it needs to build. PLAIN sketches are
# self-contained; LIB sketches pull in octagon_core; turn_counter additionally
# needs the min_spiffs partition.
SKETCHES_PLAIN := hello_board strip_test all_white tap_light
SKETCHES_LIB   := piezo_test piezo_stream eight

.PHONY: help ports ping monitor record map-piezos flash-hello flash-strip flash-white flash-tap flash-piezo flash-stream flash-eight flash-turn ota compile-all test pdf vscode upgrade check-port

help: ## list available targets
	@grep -E '^[a-z-]+:.*##' $(MAKEFILE_LIST) | awk -F':.*## ' '{printf "  make %-14s %s\n", $$1, $$2}'

ports: ## list connected boards / serial ports
	arduino-cli board list

check-port:
	@test -n "$(PORT)" || { echo "No /dev/cu.usbserial-* port found — plug in the board's UART port, or pass PORT=..."; exit 1; }

ping: check-port ## reset the board and tail serial for 5s (connection check, no reflash)
	.venv/bin/python3 scripts/ping_board.py --port $(PORT) --baud $(BAUD)

monitor: check-port ## serial monitor at 115200 (Ctrl+C to quit — frees the port for uploads)
	arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)

record: check-port ## record piezo_stream to data/piezo/ + sensitivity report (Ctrl+C to stop, or SECONDS=60)
	.venv/bin/python3 scripts/record_piezos.py --port $(PORT) --baud $(BAUD) $(if $(SECONDS),--seconds $(SECONDS),)

map-piezos: check-port ## guided piezo->side remap (each side lights, you tap it)
	.venv/bin/python3 scripts/map_piezos.py --port $(PORT) --baud $(BAUD)

flash-hello: check-port ## compile + upload hello_board (connection smoke test)
	arduino-cli compile $(VERBOSE) -u -p $(PORT) --fqbn $(FQBN) firmware/hello_board

flash-strip: check-port ## compile + upload strip_test (WS2812B smoke test)
	arduino-cli compile $(VERBOSE) -u -p $(PORT) --fqbn $(FQBN) firmware/strip_test

flash-white: check-port ## compile + upload all_white (every LED solid white — connection check)
	arduino-cli compile $(VERBOSE) -u -p $(PORT) --fqbn $(FQBN) firmware/all_white

flash-tap: check-port ## compile + upload tap_light (Phase 0 starter)
	arduino-cli compile $(VERBOSE) -u -p $(PORT) --fqbn $(FQBN) firmware/tap_light

flash-piezo: check-port ## compile + upload piezo_test (per-channel tap diagnostics)
	arduino-cli compile $(VERBOSE) $(LIBS) -u -p $(PORT) --fqbn $(FQBN) firmware/piezo_test

flash-stream: check-port ## compile + upload piezo_stream (8-channel CSV stream for make record)
	arduino-cli compile $(VERBOSE) $(LIBS) -u -p $(PORT) --fqbn $(FQBN) firmware/piezo_stream

flash-eight: check-port ## compile + upload eight (all 8 seats, clockwise, no setup mode)
	arduino-cli compile $(VERBOSE) $(LIBS) -u -p $(PORT) --fqbn $(FQBN) firmware/eight

flash-turn: check-port ## compile + upload turn_counter (uses min_spiffs partition)
	arduino-cli compile $(VERBOSE) $(LIBS) -u -p $(PORT) --fqbn $(FQBN_TURN) firmware/turn_counter

ota: ## compile turn_counter and push it over Wi-Fi (needs firmware/turn_counter/secrets.h)
	arduino-cli compile $(VERBOSE) $(LIBS) --fqbn $(FQBN_TURN) --output-dir build/turn_counter firmware/turn_counter
	.venv/bin/python3 scripts/ota_flash.py --bin build/turn_counter/turn_counter.ino.bin $(if $(HOST),--host $(HOST),)

test: ## run the host-script test suite (no board needed)
	.venv/bin/python3 -m pytest tests/ -q

compile-all: ## compile every sketch without uploading (warnings on; V=1 for full toolchain output)
	@for s in $(SKETCHES_PLAIN); do \
	  printf '\n=== %s ===\n' "$$s"; \
	  arduino-cli compile $(CHECK) --fqbn $(FQBN) firmware/$$s || exit 1; \
	done
	@for s in $(SKETCHES_LIB); do \
	  printf '\n=== %s ===\n' "$$s"; \
	  arduino-cli compile $(CHECK) $(LIBS) --fqbn $(FQBN) firmware/$$s || exit 1; \
	done
	@printf '\n=== turn_counter (min_spiffs) ===\n'
	@arduino-cli compile $(CHECK) $(LIBS) --fqbn $(FQBN_TURN) firmware/turn_counter
	@printf '\nAll sketches compiled.\n'

pdf: ## rebuild all PDFs from the markdown sources
	.venv/bin/python3 doc-src/build_pdf.py
	.venv/bin/python3 doc-src/build_simple_pdf.py
	.venv/bin/python3 doc-src/build_dry_run_pdf.py
	.venv/bin/python3 doc-src/build_tap_light_pdf.py
	.venv/bin/python3 doc-src/build_bench_guide_pdf.py

vscode: ## regenerate .vscode IntelliSense config from the installed toolchain
	python3 scripts/gen_intellisense.py

upgrade: ## upgrade cores + libraries, then refresh IntelliSense config and sanity-compile
	arduino-cli core upgrade
	arduino-cli lib upgrade
	$(MAKE) vscode
	$(MAKE) compile-all
