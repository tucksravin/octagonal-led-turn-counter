# Bench shortcuts. Port is auto-detected from the board's CP2102 UART bridge;
# override with e.g.  make monitor PORT=/dev/cu.usbserial-1130

PORT ?= $(firstword $(wildcard /dev/cu.usbserial-*))
BAUD ?= 115200
FQBN := esp32:esp32:esp32s3
# turn_counter overflows the default 1.25 MB app partition (Wi-Fi + OTA + mDNS),
# so it compiles/uploads with the min_spiffs scheme (1.9 MB app, keeps OTA).
FQBN_TURN := $(FQBN):PartitionScheme=min_spiffs
# Compile chatter: make flash-tap V=1 → full toolchain commands + all compiler
# warnings (arduino-cli hides both by default).
VERBOSE := $(if $(V),--verbose --warnings all,)

.PHONY: help ports monitor flash-hello flash-strip flash-tap flash-turn compile-all pdf check-port

help: ## list available targets
	@grep -E '^[a-z-]+:.*##' $(MAKEFILE_LIST) | awk -F':.*## ' '{printf "  make %-14s %s\n", $$1, $$2}'

ports: ## list connected boards / serial ports
	arduino-cli board list

check-port:
	@test -n "$(PORT)" || { echo "No /dev/cu.usbserial-* port found — plug in the board's UART port, or pass PORT=..."; exit 1; }

monitor: check-port ## serial monitor at 115200 (Ctrl+C to quit — frees the port for uploads)
	arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)

flash-hello: check-port ## compile + upload hello_board (connection smoke test)
	arduino-cli compile $(VERBOSE) -u -p $(PORT) --fqbn $(FQBN) firmware/hello_board

flash-strip: check-port ## compile + upload strip_test (WS2812B smoke test)
	arduino-cli compile $(VERBOSE) -u -p $(PORT) --fqbn $(FQBN) firmware/strip_test

flash-tap: check-port ## compile + upload tap_light (Phase 0 starter)
	arduino-cli compile $(VERBOSE) -u -p $(PORT) --fqbn $(FQBN) firmware/tap_light

flash-turn: check-port ## compile + upload turn_counter (uses min_spiffs partition)
	arduino-cli compile $(VERBOSE) -u -p $(PORT) --fqbn $(FQBN_TURN) firmware/turn_counter

compile-all: ## compile every sketch without uploading
	arduino-cli compile $(VERBOSE) --fqbn $(FQBN) firmware/hello_board
	arduino-cli compile $(VERBOSE) --fqbn $(FQBN) firmware/strip_test
	arduino-cli compile $(VERBOSE) --fqbn $(FQBN) firmware/tap_light
	arduino-cli compile $(VERBOSE) --fqbn $(FQBN_TURN) firmware/turn_counter

pdf: ## rebuild both PDFs from the markdown sources
	.venv/bin/python3 doc-src/build_pdf.py
	.venv/bin/python3 doc-src/build_dry_run_pdf.py
