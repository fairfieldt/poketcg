CC ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Werror -g -I. -I../src
.PHONY: all gen test clean
all: build/liboverworld.a
build/liboverworld.a: build/overworld.o build/data.o
	ar rcs $@ $^
build/overworld.o: overworld.c overworld.h gen/data.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@
build/data.o: gen/data.c gen/data.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@
gen:
	python3 tools/gen_world.py
	python3 tools/gen_native_fields.py
clean:
	rm -rf build

test: all
	$(CC) $(CFLAGS) tests/story.c build/liboverworld.a -o build/story_test
	./build/story_test
	$(CC) $(CFLAGS) tests/native_helpers.c gen/data.c ../gameplay/gameplay.c ../gameplay/gen/data.c -o build/test_native_helpers
	./build/test_native_helpers
	$(CC) $(CFLAGS) tests/campaign.c build/liboverworld.a ../gameplay/gameplay.c ../gameplay/gen/data.c -o build/campaign_test
	./build/campaign_test
	$(CC) $(CFLAGS) tests/navigation.c build/liboverworld.a -o build/navigation_test
	./build/navigation_test
