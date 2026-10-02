PROJECT := wfdigi
BUILD := build

SDCC ?= $(shell command -v sdcc 2>/dev/null || test ! -x "$(HOME)/bin/sdcc" || printf '%s' "$(HOME)/bin/sdcc")
SDAS ?= $(shell command -v sdasz80 2>/dev/null || test ! -x "$(HOME)/bin/sdasz80" || printf '%s' "$(HOME)/bin/sdasz80")
MAKEBIN ?= $(shell command -v makebin 2>/dev/null || test ! -x "$(HOME)/bin/makebin" || printf '%s' "$(HOME)/bin/makebin")
PYTHON ?= python3

CPPFLAGS := -Iinclude
SDCCFLAGS := -mz80 --std-c11 --opt-code-speed --max-allocs-per-node 10000 $(CPPFLAGS)
LDFLAGS := -mz80 --nostdlib --no-std-crt0 --code-loc 0x0120 --data-loc 0x8100

C_SOURCES := src/hardware.c src/serial.c src/config.c src/cli.c src/util.c src/pktq.c src/beacon.c src/timer.c src/modem.c src/interrupts.c src/main.c
ASM_SOURCES := src/startup.s src/isr_stubs.s
HEADERS := include/wfdigi.h include/config.h
C_OBJECTS := $(patsubst src/%.c,$(BUILD)/%.rel,$(C_SOURCES))
ASM_OBJECTS := $(patsubst src/%.s,$(BUILD)/%.rel,$(ASM_SOURCES))
OBJECTS := $(ASM_OBJECTS) $(C_OBJECTS)

.PHONY: all clean require-sdcc layout

all: require-sdcc $(BUILD)/$(PROJECT).bin layout

require-sdcc:
	@if test -z "$(SDCC)" || test -z "$(SDAS)" || test -z "$(MAKEBIN)"; then \
		echo "SDCC Z80 tools were not found. sdcc, sdasz80, and makebin must be on PATH."; \
		exit 2; \
	fi

$(BUILD):
	mkdir -p $@

$(BUILD)/%.rel: src/%.c $(HEADERS) | $(BUILD)
	$(SDCC) $(SDCCFLAGS) -c -o $@ $<

$(BUILD)/%.rel: src/%.s | $(BUILD)
	$(SDAS) -plosgff -o $@ $<

$(BUILD)/$(PROJECT).ihx: $(OBJECTS) Makefile
	$(SDCC) $(LDFLAGS) -Wl-m -o $@ $(OBJECTS)

$(BUILD)/$(PROJECT).bin: $(BUILD)/$(PROJECT).ihx
	$(MAKEBIN) -s 32768 $< $@

layout: $(BUILD)/$(PROJECT).ihx
	$(PYTHON) tools/check-layout.py $< $(BUILD)/$(PROJECT).map

clean:
	rm -rf $(BUILD)
