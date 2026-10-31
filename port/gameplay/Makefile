.PHONY: test differential gen clean
CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Werror
all: test
build/test_gameplay: gameplay.c gameplay.h gen/data.c tests/test_gameplay.c
	@mkdir -p build
	$(CC) $(CFLAGS) -I. gameplay.c gen/data.c tests/test_gameplay.c -o $@
test: build/test_gameplay
	./build/test_gameplay
build/diff_gameplay: gameplay.c gameplay.h gen/data.c tests/diff_gameplay.c ../harness/gb/gb.c
	@mkdir -p build
	$(CC) $(CFLAGS) -I. gameplay.c gen/data.c tests/diff_gameplay.c ../harness/gb/gb.c -o $@
differential: build/diff_gameplay
	./build/diff_gameplay ../../poketcg.gbc ../../poketcg.sym
gen:
	python3 gen_data.py
clean:
	rm -rf build
