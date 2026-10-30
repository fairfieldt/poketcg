# Headless DMG core for the differential test harness.
#   make            -> gbtest (test driver) and libgb.a
#   make test       -> run Blargg ROMs from $(ROMS) (see gbtest.c)
CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -pedantic
ROMS    ?= roms

all: gbtest libgb.a

gb.o: gb.c gb.h
	$(CC) $(CFLAGS) -c -o $@ gb.c

libgb.a: gb.o
	ar rcs $@ gb.o

gbtest: gbtest.c gb.c gb.h
	$(CC) $(CFLAGS) -o $@ gbtest.c gb.c

test: gbtest
	@for r in $(ROMS)/cpu_instrs/individual/*.gb $(ROMS)/instr_timing/instr_timing.gb; do \
		./gbtest blargg "$$r" | head -1; done

clean:
	rm -f gbtest gb.o libgb.a

.PHONY: all test clean
