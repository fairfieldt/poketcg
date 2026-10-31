#!/bin/sh
set -eu
cd "$(dirname "$0")/../../../../.."
make -C port/overworld build/liboverworld.a
mkdir -p port/frontends/n64/adventure/build/host
cc -std=c11 -Wall -Wextra -Werror -Iport/src -Iport/overworld \
 -Iport/gameplay -Iport/world3d \
 port/frontends/n64/adventure/tests/visibility.c \
 port/frontends/n64/adventure/presentation.c port/overworld/build/liboverworld.a \
 -lm -o port/frontends/n64/adventure/build/host/test_visibility
port/frontends/n64/adventure/build/host/test_visibility
python3 port/frontends/n64/adventure/tools/check_visibility_bounds.py
