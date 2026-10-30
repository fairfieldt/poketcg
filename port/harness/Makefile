# Reference side of the differential test harness: runs duels in the original
# ROM inside the headless DMG core (gb/).
#
#   make              -> refrun (CLI) and libref.a (ref.c + gb.c)
#   make validate     -> AI-vs-AI and scripted-human duels over several deck pairs/seeds
#
# ROM/SYM default to the disassembly build at the repo root (`make` there first).
CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -pedantic
ROM     ?= ../../poketcg.gbc
SYM     ?= ../../poketcg.sym

all: refrun libref.a

ref.o: ref.c ref.h gb/gb.h
	$(CC) $(CFLAGS) -c -o $@ ref.c

gb.o: gb/gb.c gb/gb.h
	$(CC) $(CFLAGS) -c -o $@ gb/gb.c

libref.a: ref.o gb.o
	$(AR) rcs $@ ref.o gb.o

refrun: refrun.c ref.h libref.a
	$(CC) $(CFLAGS) -o $@ refrun.c libref.a

# deck ids (*_DECK_ID = *_DECK - 2): 3 CHARMANDER_AND_FRIENDS 5 SQUIRTLE_AND_FRIENDS
# 7 BULBASAUR_AND_FRIENDS 9 LIGHTNING_AND_FIRE 10 WATER_AND_FIGHTING 11 GRASS_AND_PSYCHIC
# 12-15 LEGENDARY_MOLTRES/ZAPDOS/ARTICUNO/DRAGONITE (Grand Masters)
# club masters: 16 FIRST_STRIKE 17 ROCK_CRUSHER 18 GO_GO_RAIN_DANCE 19 ZAPPING_SELFDESTRUCT
# 20 FLOWER_POWER 21 STRANGE_PSYSHOCK 22 WONDERS_OF_SCIENCE 23 FIRE_CHARGE
# 24-27 IM/POWERFUL/INVINCIBLE/LEGENDARY_RONALD 28-52 club members, 52 IMAKUNI
VALIDATE_PAIRS ?= 3,5 5,7 7,3 16,17 18,19 20,21 22,23 12,13 24,27 14,15
validate: refrun
	@for p in $(VALIDATE_PAIRS); do \
		./refrun $(ROM) $(SYM) --decks $${p%,*} $${p#*,} --seed 0x12 0x34 0 --count 4 | tail -1 || exit 1; \
	done
	@for p in $(VALIDATE_PAIRS); do \
		./refrun $(ROM) $(SYM) --decks $${p%,*} $${p#*,} --seed 0x56 0x78 0 --human 7 --count 4 | tail -1 || exit 1; \
	done

clean:
	rm -f refrun ref.o gb.o libref.a

.PHONY: all validate clean
