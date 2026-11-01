#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/host
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
 -Itests/profiles_mock -I../../../src -I../../../gameplay -I../../../overworld \
 tests/profiles_test.c ../../../gameplay/gameplay.c ../../../gameplay/gen/data.c \
 ../../../overworld/overworld.c ../../../overworld/gen/data.c -o build/host/test_profiles
./build/host/test_profiles
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
 -Itests/profiles_mock -I../../../src -I../../../gameplay -I../../../overworld \
 tests/deck_editor.c ../../../gameplay/gameplay.c ../../../gameplay/gen/data.c \
 ../../../overworld/overworld.c ../../../overworld/gen/data.c -o build/host/test_deck_editor
./build/host/test_deck_editor
