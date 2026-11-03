#!/usr/bin/env bash
# Standalone duels with optional browser-local ROM card art.
set -euo pipefail
WEB="$(cd "$(dirname "$0")" && pwd)"
export DIST="${DIST:-$WEB/dist-multiplayer}"
# Refuse a reused output directory so old bundled art cannot leak into this build.
if [ -d "$DIST" ] && [ -n "$(ls -A "$DIST")" ]; then
  echo "Choose an empty DIST directory (or remove $DIST before rebuilding)." >&2
  exit 1
fi
export MULTIPLAYER=1
exec "$WEB/build.sh"
