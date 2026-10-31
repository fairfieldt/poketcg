# Host-only attack audit. The Game Boy emulator is an oracle in tests only.
CC ?= cc
PORT := ../..
REPO := ../../..
BUILD ?= $(PORT)/build/effects-audit
CFLAGS := -std=c11 -O2 -g -I$(PORT)/src -DTCG_NO_DEFAULT_FATAL -Wall -Wno-unused-parameter
ENGINE := $(wildcard $(PORT)/src/*.c $(PORT)/src/gen/*.c $(PORT)/src/effects/*.c $(PORT)/src/ai/*.c)
HEADERS := $(wildcard $(PORT)/src/*.h $(PORT)/src/gen/*.h $(PORT)/src/effects/*.h)

.PHONY: test catalog units mechanics shared-units focus scratch-probe
test: catalog units mechanics shared-units

catalog:
	python3 catalog.py

$(BUILD)/effect_units: effect_units.c effect_unit_table.inc $(PORT)/harness/unitdiff.c $(PORT)/harness/gb/gb.c $(ENGINE) $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ effect_units.c $(PORT)/harness/gb/gb.c $(ENGINE)

$(BUILD)/mechanics: mechanics.c $(ENGINE) $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ mechanics.c $(ENGINE)

units: $(BUILD)/effect_units
	$< $(REPO)/poketcg.gbc $(REPO)/poketcg.sym 24

mechanics: $(BUILD)/mechanics
	$<

shared-units:
	$(MAKE) -C $(PORT) BUILD=build/effects-audit harness
	$(BUILD)/harness/unitdiff $(REPO)/poketcg.gbc $(REPO)/poketcg.sym 200

focus:
	python3 focus.py

scratch-probe:
	python3 scratch_probe.py
