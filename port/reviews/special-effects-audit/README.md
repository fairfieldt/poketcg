# Pokémon special attack audit

The complete non-Power attack catalog is accounted for, and this pass found no confirmed new attack-rules defect. One strict ROM comparison exposes an existing, documented approximation of a discarded UI register after cancelling Amnesia. That boundary is retained and explained below; the default differential harness remains strict.

All 187 Pokémon card records match the original ROM bytes. Their 319 named non-Power attacks comprise 70 ordinary attacks and 249 distinct effect command chains. Every chain matches both the original assembly and generated C in command order and function identity, and all 431 referenced functions have strong C implementations. The 26 Power entries belong to the separate Power audit.

## Execution coverage

| Check | Result |
|---|---|
| Rotating-deck baseline, original ROM lockstep | 120/120 duels agree |
| Initial focused human batch | 20/20 agree |
| Bounded individual-card follow-up | 124/125 strictly agree; the remaining duel is the Amnesia scratch case below |
| Final presentation-source regression | 40/40 agree |
| Direct attack-function ROM comparisons | 103 functions × 24 operand fixtures, all pass |
| Shared damage/status/energy/evolution/card helpers | 76 routines × 200 iterations, plus 200 RNG checks, all pass |
| Focused real-engine assertions | 30 cases across both turn sides, all pass |

Lockstep reached 333 of the 431 attack-related functions, including 245 of 317 runtime functions. Direct function comparisons increase the union to 346 functions, including 258 runtime functions. A trace hit verifies compared state on entry; it does not prove all branches or the function's return. The direct tests compare post-return memory and meaningful initial-check carry flags using the existing oracle's stack/bank/IE exclusions. Their structured operand fixtures are separate from complete legal duels.

[coverage.json](coverage.json) records each function's source location, stage, cards, strict trace hits and direct-unit coverage. [uncovered-runtime.txt](uncovered-runtime.txt) explicitly lists the 59 runtime functions whose original-ROM execution was not observed. Some are exercised by the separate C assertion cases, but those are not counted as ROM coverage. Untested conditions and AI branches remain possible.

The per-card batch had a 180-second launch deadline; its final in-flight test finished at 185.1 seconds. It stopped instead of extending stochastic testing to chase 100 percent. The full catalog remains in [inventory.json](inventory.json), with descriptions, commands, descriptive mechanic tags and conservatively parsed transitive C calls.

## Amnesia cancellation fidelity boundary

The strict reproduction is a Poliwhirl-focused human duel, seed `0bd823` (numeric seed 9071). After cancelling Amnesia's defending-attack selector, the next `UseAttackOrPokemonPower` entry differs only at `0xffa0`, `hTempList[0]`: C has `0`, the ROM has `13`. See [the original failing log](poliwhirl-repro.log).

The original `PlayerPickAttackForAmnesia` writes register E even on cancellation. `HandleDefendingPokemonAttackSelection` catches B before its menu handler; E retains a cursor-drawing coordinate. The C port explicitly documents substituting zero when the selection leaves that output untouched. This byte is discarded when the attack is cancelled and overwritten by the next successful selection.

The optional [diagnostic patch](amnesia-scratch-probe.patch) changes only comparison policy. It permits exactly that address/value pair at the three known post-cancel checkpoints, after a known cancelled Amnesia request. It does not write engine state, and normal comparison resumes as soon as the byte matches. The [qualified full replay](amnesia-scratch-probe.log) reaches 51 turns, 9,330 events and 818 matching RNG events, with only three discarded-scratch comparisons and no allowance outstanding at the end. Its conclusion is **state/RNG agree except documented discarded UI scratch**.

New assertions separately cover Poliwhirl and Slowpoke, both turn sides: cancel preserves card/HP/disabled-attack state and RNG; a subsequent fresh selection disables attack 1 correctly. No runtime change is recommended to reproduce a timing-dependent, discarded UI register. The standard harness and production engine are untouched by this audit.

## Mechanism checks and source review

The catalog indexes status, coins, damage scaling, recoil, Bench damage, healing, protection, turn restrictions, switching, energy removal, draw/search, copying, type/weakness/resistance, evolution and hand manipulation. Tags are indexing aids, not branch-coverage claims.

The focused engine cases specifically verify:

- Metronome checks copied energy requirements, selects/discards two Energy cards and deals the copied Fire Spin's 100 damage; self-copy rejection and cancellation preserve the source behavior.
- Mirror Move rejects absent history and copies exact damage, Poison and substatus. Both Amnesia implementations cancel safely, then disable the newly selected attack.
- Ditto Morph copies the target ID, restores its maximum HP, clears changed type/status, and leaves the original target card in the Deck.
- Ninetales Mix-Up preserves Hand size, Pokémon count and non-Pokémon cards while shuffling replacements.
- Moltres Wildfire discards the selected quantity of Fire Energy and clamps milling to the opponent's one remaining card.
- Mew Devolution Beam handles either side's Bench, retains damage through devolution including its KO threshold, returns the evolved card to Hand, and resets stage/flags/changed type.

Risk-focused assembly/C review covered the shared Metronome and Mirror Move routes, Amnesia cancellation, Devolution Beam's side/protection paths, Morph's evolution/ID behavior, Mix-Up's preserved count, and Wildfire's count/clamp. Original quirks remain source-authoritative; for example, Devolution Beam's stale-flags branch is explicitly preserved in the C implementation. No new discrepancy in these rules was established. Selector affordances and Power interactions are covered by the sibling frontend and Power audits.

## Reproduce

From the repository root, with the existing original `poketcg.gbc` and `poketcg.sym`:

```sh
make -C port/tests/special_effects test
```

This runs the catalog parity check, 103 × 24 direct ROM units, 30 mechanics cases and shared helper differentials. Individual targets are `catalog`, `units`, `mechanics` and `shared-units`.

The bounded stochastic follow-up and the narrowly qualified replay are separate:

```sh
make -C port/tests/special_effects focus
make -C port/tests/special_effects scratch-probe
```

The focus target uses the stored per-card plan and writes its own strict failure if the documented Amnesia case recurs. To reproduce only that strict failure, after building the normal harness:

```sh
port/build/effects-audit/harness/difftest poketcg.gbc poketcg.sym \
  port/build/effects-audit/harness/trace_points.txt \
  --human --focus port/reviews/special-effects-audit/focused/47.txt \
  --seeds 9071 1 --verbose
```

[validation.json](validation.json) contains the detailed results and limits. [entrypoint-test.log](entrypoint-test.log) verifies the runnable entrypoint. Baseline and per-card raw logs/coverage are retained alongside it. No ares, GPU or physical-console run is part of these host engine checks.
