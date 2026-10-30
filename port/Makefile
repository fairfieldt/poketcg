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

.PHONY: all objs gen clean test harness
