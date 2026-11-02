#!/usr/bin/env bash
# Shared portable-C story/gameplay/duel engines and browser platform -> dist/.
set -euo pipefail
WEB="$(cd "$(dirname "$0")" && pwd)"
PORT="$(cd "$WEB/../.." && pwd)"
EMCC="${EMCC:-emcc}"
PYTHON="${PYTHON:-python3}"
if [ -z "${WASM_OPT:-}" ]; then
  if command -v wasm-opt >/dev/null 2>&1; then WASM_OPT=wasm-opt
  else WASM_OPT="$("$PYTHON" -c 'import pathlib,shutil; p=pathlib.Path(shutil.which("emcc")).resolve(); print(next(q for q in [p.parent / "binaryen/bin/wasm-opt",p.parent.parent / "libexec/binaryen/bin/wasm-opt",p.parent.parent / "bin/wasm-opt",p.parent.parent / "binaryen/bin/wasm-opt"] if q.exists()))')"
  fi
fi
mkdir -p "$WEB/build" "$WEB/dist"
N64="$PORT/frontends/n64/adventure"
SOURCES=("$PORT"/src/*.c "$PORT"/src/gen/*.c "$PORT"/src/effects/*.c "$PORT"/src/ai/*.c
 "$PORT"/gameplay/*.c "$PORT"/gameplay/gen/*.c "$PORT"/overworld/*.c "$PORT"/overworld/gen/*.c
 "$WEB/wasm/web_host.c" "$WEB/wasm/adventure_host.c"
 "$N64/menus.c" "$N64/profiles.c" "$N64/extra_ui.c" "$N64/presentation.c")
echo "Compiling full portable-C game"
"$EMCC" -O2 -std=c11 -DTCG_NO_DEFAULT_FATAL -DTCG_WEB_ADVENTURE=1 -I"$PORT/src" -I"$PORT/gameplay" -I"$PORT/overworld" -I"$PORT/world3d" -I"$WEB/wasm/adventure" \
 -Wall -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable \
 -sSTANDALONE_WASM=1 --no-entry -sINITIAL_MEMORY=16777216 -sSTACK_SIZE=1048576 \
 "${SOURCES[@]}" -o "$WEB/build/tcg.raw.wasm"
echo "Asyncifying browser callbacks"
"$WASM_OPT" "$WEB/build/tcg.raw.wasm" -o "$WEB/build/tcg.wasm.next" \
 --enable-bulk-memory --enable-sign-ext --enable-mutable-globals --enable-nontrapping-float-to-int --asyncify \
 --pass-arg=asyncify-imports@env.js_event,env.js_request,env.js_adventure -O2
mv "$WEB/build/tcg.wasm.next" "$WEB/dist/tcg.wasm"
if [ "${CODE_ONLY:-0}" = 1 ]; then node "$WEB/tools/build_tutorial_diary.mjs"; cp -r "$WEB/src/." "$WEB/dist/"; exit 0; fi
"$PYTHON" "$WEB/tools/gen_assets.py" "$WEB/dist"
"$PYTHON" "$PORT/world3d/tools/sync_characters.py" --strict
"$PYTHON" "$PORT/world3d/tools/check_actor_grounding.py"
"$PYTHON" "$WEB/tools/pack_adventure.py"
node "$WEB/tools/build_tutorial_diary.mjs"
cp -r "$WEB/src/." "$WEB/dist/"
printf '%s\n' '{"config":"full-game","effects":["all"],"ai":["all"],"skipped":[]}' > "$WEB/dist/build-info.json"
echo "Built $WEB/dist"
