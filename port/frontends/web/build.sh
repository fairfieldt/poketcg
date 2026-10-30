#!/usr/bin/env bash
# Builds the web frontend into port/frontends/web/dist/:
#   dist/tcg.wasm        the duel engine (clang --target=wasm32, no emscripten) + Asyncify
#   dist/build-info.json which optional engine sources were linked
#   dist/gfx/*           card art sprite sheet and symbols (from src/gfx, see tools/gen_assets.py)
#   dist/js/gen/*.js     constants, card gfx map (generated)
#   dist/*               static files copied from src/
#
# Engine sources: port/src/*.c and port/src/gen/*.c are required. port/src/effects/*.c and
# port/src/ai/*.c are optional because they are being ported concurrently: every optional file
# that fails to compile is skipped, and if the link fails the script retries without the AI
# files, then without the effect files, then without both (the engine then links against the
# weak stubs in gen/effect_stubs.c and ai_stubs.c, which call tcg_fatal when reached).
#
# Environment: CORE_ONLY=1 links only the required sources (to try the stub fallback), CLANG (default clang), WASM_OPT (default: wasm-opt from PATH or
# /home/user/tools/npm/node_modules/.bin/wasm-opt), PYTHON (default python3).
set -euo pipefail

WEB="$(cd "$(dirname "$0")" && pwd)"
PORT="$(cd "$WEB/../.." && pwd)"
SRC="$PORT/src"
OUT="$WEB/dist"
OBJ="$WEB/build/obj"
CLANG="${CLANG:-clang}"
PYTHON="${PYTHON:-python3}"
if [ -z "${WASM_OPT:-}" ]; then
	if command -v wasm-opt >/dev/null 2>&1; then WASM_OPT=wasm-opt
	else WASM_OPT=/home/user/tools/npm/node_modules/.bin/wasm-opt; fi
fi

CFLAGS=(--target=wasm32 -O2 -std=c11 -ffreestanding -nostdlib -fno-builtin-printf
	-DTCG_NO_DEFAULT_FATAL -I"$SRC" -I"$WEB/wasm/include"
	-Wall -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable)
LDFLAGS=(--target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export-memory
	-Wl,-z,stack-size=1048576 -Wl,--initial-memory=8388608 -Wl,--max-memory=33554432)

rm -rf "$OBJ"
mkdir -p "$OBJ" "$OUT"

compile() { # src obj -> 0/1
	"$CLANG" "${CFLAGS[@]}" -c "$1" -o "$2" 2>"$2.log"
}

objname() { local rel="${1#$PORT/}"; echo "$OBJ/${rel//\//_}.o"; }

echo "== compiling engine (wasm32)"
CORE=()
for f in "$SRC"/*.c "$SRC"/gen/*.c "$WEB"/wasm/*.c; do
	o="$(objname "$f")"
	if ! compile "$f" "$o"; then
		cat "$o.log" >&2
		echo "error: required source $f does not compile" >&2
		exit 1
	fi
	CORE+=("$o")
done
EFFECTS=(); AI=(); SKIPPED=()
for f in "$SRC"/effects/*.c "$SRC"/ai/*.c; do
	[ -f "$f" ] || continue
	if [ -n "${CORE_ONLY:-}" ]; then SKIPPED+=("${f#$PORT/} (CORE_ONLY)"); continue; fi
	o="$(objname "$f")"
	if compile "$f" "$o"; then
		case "$f" in */effects/*) EFFECTS+=("$o") ;; *) AI+=("$o") ;; esac
	else
		SKIPPED+=("${f#$PORT/} (compile error)")
		echo "   skipping ${f#$PORT/}: does not compile ($(grep -c 'error:' "$o.log" || true) errors)"
	fi
done

echo "== linking"
RAW="$WEB/build/tcg.raw.wasm"
LINKLOG="$WEB/build/link.log"
: >"$LINKLOG"
link() { # label objs...
	local label="$1"; shift
	echo "--- link attempt: $label" >>"$LINKLOG"
	"$CLANG" "${LDFLAGS[@]}" -o "$RAW" "$@" 2>>"$LINKLOG"
}
NE=${#EFFECTS[@]}; NA=${#AI[@]}
if link "core+effects+ai" "${CORE[@]}" "${EFFECTS[@]}" "${AI[@]}"; then :
elif [ "$NA" -gt 0 ] && link "core+effects" "${CORE[@]}" "${EFFECTS[@]}"; then
	SKIPPED+=("src/ai/*.c (link errors)"); AI=()
elif [ "$NE" -gt 0 ] && [ "$NA" -gt 0 ] && link "core+ai" "${CORE[@]}" "${AI[@]}"; then
	SKIPPED+=("src/effects/*.c (link errors)"); EFFECTS=()
elif [ $((NE + NA)) -gt 0 ] && link "core" "${CORE[@]}"; then
	[ "$NE" -gt 0 ] && SKIPPED+=("src/effects/*.c (link errors)")
	[ "$NA" -gt 0 ] && SKIPPED+=("src/ai/*.c (link errors)")
	EFFECTS=(); AI=()
else
	cat "$LINKLOG" >&2
	echo "error: the engine core does not link" >&2
	exit 1
fi
CONFIG="core"
[ ${#EFFECTS[@]} -gt 0 ] && CONFIG="$CONFIG+effects(${#EFFECTS[@]})"
[ ${#AI[@]} -gt 0 ] && CONFIG="$CONFIG+ai(${#AI[@]})"
echo "   linked: $CONFIG (failed attempts are logged in build/link.log)"
for s in "${SKIPPED[@]}"; do echo "   not linked: $s"; done

echo "== asyncify ($WASM_OPT)"
"$WASM_OPT" "$RAW" -o "$OUT/tcg.wasm" --enable-bulk-memory --enable-sign-ext --enable-mutable-globals \
	--asyncify --pass-arg=asyncify-imports@env.js_event,env.js_request -O2

{
	echo "{"
	echo "  \"config\": \"$CONFIG\","
	printf '  "effects": ['; first=1; for o in "${EFFECTS[@]}"; do [ $first = 1 ] || printf ', '; first=0; printf '"%s"' "$(basename "$o" .c.o | sed 's/^src_//')"; done; echo "],"
	printf '  "ai": ['; first=1; for o in "${AI[@]}"; do [ $first = 1 ] || printf ', '; first=0; printf '"%s"' "$(basename "$o" .c.o | sed 's/^src_//')"; done; echo "],"
	printf '  "skipped": ['; first=1; for s in "${SKIPPED[@]}"; do [ $first = 1 ] || printf ', '; first=0; printf '"%s"' "$s"; done; echo "],"
	echo "  \"built\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\""
	echo "}"
} >"$OUT/build-info.json"

echo "== assets"
PYTHONDONTWRITEBYTECODE=1 "$PYTHON" "$WEB/tools/gen_assets.py" "$OUT"

echo "== static files"
cp -r "$WEB/src/." "$OUT/"
echo "== done: $OUT ($(du -sh "$OUT" | cut -f1))"
