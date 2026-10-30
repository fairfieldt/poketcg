# Portable C port of the Pokémon TCG duel engine.
#
#   make            build the engine library (build/libtcg.a)
#   make objs       compile every engine object (no link), useful while porting
#   make gen        regenerate src/gen/* from the disassembly (needs `make` at the repo root first)
#   make test       build and run the unit tests
#   make harness    build the differential test harness (needs the original ROM, see harness/)

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-parameter -Isrc
BUILD   ?= build

ENGINE_SRCS := $(wildcard src/*.c) $(wildcard src/gen/*.c) $(wildcard src/effects/*.c) $(wildcard src/ai/*.c)
ENGINE_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(ENGINE_SRCS))

all: $(BUILD)/libtcg.a

objs: $(ENGINE_OBJS)

$(BUILD)/libtcg.a: $(ENGINE_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(BUILD)/%.o: %.c $(wildcard src/*.h src/gen/*.h src/effects/*.h src/ai/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

gen:
	python3 tools/gen_constants.py
	python3 tools/gen_wram.py
	python3 tools/gen_data.py

clean:
	rm -rf $(BUILD)

# ---- differential test harness (needs ../poketcg.gbc and ../poketcg.sym: run `make` at the repo root)
ROM ?= ../poketcg.gbc
SYM ?= ../poketcg.sym
HARNESS_ENGINE := $(ENGINE_SRCS)
HARNESS_CFLAGS := -std=c11 -O2 -g -Isrc -DTCG_NO_DEFAULT_FATAL -Wall -Wno-unused-parameter

harness: $(BUILD)/harness/difftest $(BUILD)/harness/unitdiff $(BUILD)/harness/trace_points.txt

$(BUILD)/harness/trace_points.txt: $(ENGINE_SRCS) tools/trace_points.py
	@mkdir -p $(dir $@)
	python3 tools/trace_points.py src > $@

$(BUILD)/harness/difftest: harness/difftest.c harness/ref.c harness/ref.h harness/gb/gb.c $(HARNESS_ENGINE)
	@mkdir -p $(dir $@)
	$(CC) $(HARNESS_CFLAGS) -DTCG_TRACE -o $@ harness/difftest.c harness/ref.c harness/gb/gb.c $(HARNESS_ENGINE)

$(BUILD)/harness/unitdiff: harness/unitdiff.c harness/gb/gb.c $(HARNESS_ENGINE)
	@mkdir -p $(dir $@)
	$(CC) $(HARNESS_CFLAGS) -o $@ harness/unitdiff.c harness/gb/gb.c $(HARNESS_ENGINE)

# unit fuzzing of home/core routines, then lockstep duels (AI vs AI over many deck pairs and seeds)
test: harness
	$(BUILD)/harness/unitdiff $(ROM) $(SYM) 2000
	$(BUILD)/harness/difftest $(ROM) $(SYM) $(BUILD)/harness/trace_points.txt --all-pairs --seeds 1 100

.PHONY: all objs gen clean test harness
