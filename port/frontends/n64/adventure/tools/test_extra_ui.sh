#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/host
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
 -Itests/extra_ui_mock -I../../../src -I../../../gameplay -I../../../overworld \
 tests/extra_ui.c extra_ui.c ../../../gameplay/gameplay.c ../../../gameplay/gen/data.c \
 ../../../overworld/gen/data.c -o build/host/test_extra_ui
./build/host/test_extra_ui
