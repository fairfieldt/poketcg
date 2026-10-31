# Amy winning-duel checkpoint

The private staging program plays a fresh ordinary human duel using the original Pikachu Deck against Amy's original Go Go Rain Dance Deck. Seed281 naturally reaches turn28 with Raichu using Agility against Amy's only remaining Pokémon, Goldeen. Forty damage knocks it out. The source engine awards a Prize, reports Amy has no Pokémon left, announces the actual win and returns normally. No deck, HP, Energy, RNG or save bytes are patched.

The 1,660-byte checkpoint is the engine's normal save callback before the attack. It stays in the ignored build directory. The generator resumes it, replays all12 recorded request answers, verifies every request state/RNG hash and verifies the complete final state/RNG hash. [transcript.txt](transcript.txt), [answers.json](answers.json) and [checkpoint.json](checkpoint.json) describe that proof. They are host-engine evidence; the paired final movies provide native capture evidence.

Reproduce from the repository root:

```sh
make -C port BUILD=build/trailer-amy CFLAGS='-O2 -g -std=c11 -Wall -Wextra -Wno-unused-parameter -Isrc -DTCG_NO_DEFAULT_FATAL' objs
python3 - <<'PYTHON'
from pathlib import Path
import subprocess
work = Path('port/frontends/n64/adventure/build/trailer-amy')
work.mkdir(parents=True, exist_ok=True)
objects = sorted(str(p) for p in Path('port/build/trailer-amy/src').rglob('*.o'))
subprocess.run(['cc', '-std=c11', '-O2', '-g', '-Iport/src',
    'port/frontends/n64/adventure/tests/trailer_amy_stage.c', *objects,
    '-o', str(work/'stage')], check=True)
subprocess.run([str(work/'stage'), str(work)], check=True)
PYTHON
```

For an existing private object tree previously compiled without `TCG_NO_DEFAULT_FATAL`, recompile `port/src/tcg.c` with that define first, or use a fresh private build directory. The generator's fatal handler must replace the default handler so a rejected search seed can stop cleanly.
