# Pokémon Power source and engine audit

Audited after commit `74138205` (the published Venusaur selector fix), against
the original disassembly in this repository. No additional Power rules defect
was found. This audit adds tests and a catalog; it changes no production rules,
cards, RNG, saves or engine answers.

The separate frontend audit found missing effect-selection cues and card-order
glyphs, an unformatted color-selection prompt, and a Peek/Grass-selector mode
collision. Those presentation changes were independently reviewed here: the
shared hooks emit drawing events only, preserve original request/ACK order and
all WRAM/HRAM, and clear their state on cancellation/screen exit. The native
and browser frontends use the same new enum values. Their visual/input gates
are owned by the frontend agent; the engine tests below pass with those hooks.

## Evidence

- `make -C port poweraudit`: **624 actual-engine outcome cases pass**.
- The same cases pass AddressSanitizer and UndefinedBehaviorSanitizer.
- `make -C port powercatalog`: **26 card variants, 26 distinct command chains**,
  comprising **10 manual, 12 passive, 4 on-play**. Each generated card's identity,
  Power category, attack slot and effect pointer is checked against the original
  card record. Every phase/function pair is compared to the original command
  array. Both Venusaur and both Dragonite variants are present.
- Original-ROM `unitdiff poketcg.gbc poketcg.sym 300`: **all 76 routines pass**,
  with 300 randomized calls per routine plus 300 RNG comparisons. These compare
  relevant return registers and WRAM/HRAM (excluding the original call stack),
  including capability/Muk counts, Clairvoyance, Rain Dance, Prehistoric Power,
  retreat cost, Energy Burn, changed colors, damage reduction and turn resets.
- Logs: [engine.log](engine.log), [sanitizers.log](sanitizers.log),
  [unitdiff.log](unitdiff.log). [catalog.json](catalog.json) includes exact
  original card source locations and every command phase/function.

The outcome harness calls the real dispatcher and UI shims, supplies only human
answers, and uses the real RNG. It also exercises the private production
`PlayEnergyCard`, `PlayPokemonCard` and `DuelMenu_PkmnPower` callers. It does not
replace an effect function, coin toss, card movement, damage calculation or
knockout/prize routine. The original ROM comparisons use the existing Game Boy
CPU harness; no N64 emulator or GPU is needed for these tests.

## Rules and coverage by Power

| Card | Power | Verified rule/outcome |
| --- | --- | --- |
| Venusaur Lv64 | Solar Power | Clears statuses on **both** Active Pokémon; requires at least one status; once per turn; Bench use works while own Active is asleep. Actual Power menu tested with source deck index30 at slot2. |
| Venusaur Lv67 | Energy Trans | Moves exact attached basic Grass Energy between own Pokémon, repeatedly; Fire Energy untouched; recipient cancel returns the energy to its donor; exit conserves cards/hand and does not set a once-per-turn flag. |
| Vileplume | Heal | Requires damage; real heads/tails paths both covered; removes one counter from a damaged chosen Pokémon on heads; unsuccessful/cancelled target attempts retry; either coin result consumes the Power for the turn. |
| Venomoth | Shift | Chooses a non-Colorless type present in either play area; invalid type retries; cancel leaves color/used flag untouched; successful use changes its type and consumes the turn's use. Muk clears changed types on both sides. |
| Tentacool | Cowardice | Cannot be used on its placement turn or with no Bench; Bench return needs no replacement selector; Active return can cancel or choose a replacement; returns Tentacool, discards attachments, compacts play area. |
| Mankey | Peek | Once per turn; own top deck, opponent prize and random opponent hand covered; no card is drawn/revealed to inventory; invalid/taken prize retries, B from area selection retries, and opponent inspection restores the original turn. |
| Alakazam | Damage Swap | Repeatedly moves own damage counters; rejects a recipient that would be knocked out; recipient cancel restores the counter; donor exit stops; no once-per-turn flag; total damage conserved. |
| Slowbro | Strange Behavior | Takes counters from other damaged own Pokémon; rejects itself as donor; cannot reduce itself to0 HP; repeated moves stop at10 HP; no once-per-turn flag. |
| Gengar | Curse | Requires at least two opposing Pokémon and opposing damage; once per turn; rejects identical donor/recipient; recipient cancel returns to donor selection; donor cancel aborts; can KO a Bench Pokémon and correctly awards a prize/removes the KO. |
| Dragonite Lv45 | Step In | Bench only; switches itself into Active; used flag follows the switching card; once per turn. |
| Muk | Toxic Gas | Suppresses Powers on either side; poisoned Active Muk still suppresses; asleep/confused/paralyzed Active Muk does not; a Bench Muk remains effective; placing Muk clears both sides' changed types. |
| Charizard | Energy Burn | Active capability gate; converts colored-energy counts to Fire, retaining the source's Double Colorless bookkeeping. Original-ROM differential covers the conversion's exact state. |
| Blastoise | Rain Dance | Own capable Blastoise permits repeated Water Energy attachments to Water Pokémon even after a normal attachment; extra attachments do not consume the ordinary attachment; a non-Water recipient still obeys the one-attachment rule. Both player sides use the actual hand-play caller. |
| Omanyte | Clairvoyance | Own capable Omanyte enables the passive; Muk suppresses it; incapable Active is ignored while Bench remains eligible. |
| Machamp | Strikes Back | Returns10 damage after nonzero attack damage; status/Muk/Power damage suppress it; can KO the attacker. |
| Kabuto | Kabuto Armor | Attack damage halves rounded down to10; status/Muk disable it; Power damage bypasses it. Original source high-byte arithmetic quirk is preserved. |
| Aerodactyl | Prehistoric Power | Either capable side prevents evolution; Muk disables the ban. The existing26-case evolution regression separately tests actual evolution rejection and recovery. |
| Haunter Lv17 | Transparency | Real coin outcomes both covered; heads blocks attack damage/effects; status/Muk capability rules apply; Power damage bypasses it without tossing a coin. |
| Mr. Mime | Invisible Wall | Attacks below30 retain damage; attacks of30+ become0; status/Muk disable it; Power damage bypasses it, including actual Zapdos on-play damage. |
| Mew Lv8 | Neutralizing Shield | Blocks an evolved attacker, permits a Basic attacker, and permits self-damage; status/Muk disable it. |
| Dodrio | Retreat Aid | Only Bench copies reduce retreat cost; multiple copies saturate at0; an Active Dodrio alone does not help; Muk disables the reduction. |
| Snorlax | Thick Skinned | Prevents queued status when capable; an already asleep Snorlax or Muk allows the status; on-play Quickfreeze also respects this defense. Poison alone is not a Power-incapacitating status. |
| Moltres Lv37 | Firegiver | Triggers when played, not from the manual menu; draws1–4 available Fire Energy (capped by supply), none if unavailable; moves only eligible deck cards and updates hand/deck counts; shuffles afterward, including the no-Fire case. |
| Articuno Lv37 | Quickfreeze | On-play real coin toss; heads paralyzes the opposing Active, tails does not; Thick Skinned prevents the status; Muk suppresses the trigger entirely. |
| Zapdos Lv68 | Peal of Thunder | On-play30 damage to one real random Pokémon on either side, rerolling its own slot; both sides selected across seeds; source remains undamaged; bypasses Invisible Wall as Power damage. |
| Dragonite Lv41 | Healing Wind | On-evolution trigger heals up to20 from each own Pokémon without exceeding max HP; healthy cards unchanged; actual Dragonair→Dragonite hand evolution included. |

All10 manual chains are tested through their actual `INITIAL_EFFECT_2` command
under normal/poisoned, asleep, live Muk, poisoned Muk and asleep Muk conditions
on both player sides. All six once-per-turn checks reject reuse and become
available after the actual turn-reset routine clears the flag. All12 passive
and all4 on-play chains reject manual activation through `INITIAL_EFFECT_1`.
All4 on-play triggers are tested under Muk on both sides with no coin/energy/HP
change and unchanged RNG.

## Fidelity boundaries

The original source—not later printed-card errata—is the authority here.
Examples preserved include Kabuto's documented `sla d / rr e` high-byte quirk,
the original Energy Burn count bookkeeping, and Peek committing its used flag
before inspection and requiring a final target rather than permitting a full
abort. `hTempList[0]` and `hTemp_ffa0` are the **same HRAM byte**, so Curse's
apparently different used-flag reference is correct.

The original AI policy is selective: its general Power loop considers Heal,
Shift, Peek, Strange Behavior and Curse; Energy Trans, Cowardice, Damage Swap
and Go Go Rain Dance have separate AI routines. It does not invent uses for
every manual Power. Those policy routes remain as the source specifies; this
focused outcome suite does not claim exhaustive AI strategic coverage.

These are deterministic engine/source regressions, not a visual or controller
sign-off. They do not exhaust every possible60-card state or link-cable play;
the portable port intentionally has no original link-cable transport. Native
P1/P2 selectors, marker redraws, browser lifecycle, full duels and source
lockstep runs are complementary gates owned by the other audit agents.

## Reproduction

```sh
make -C port poweraudit
make -C port BUILD=build/power-audit-sanitize \
  HARNESS_CFLAGS='-std=c11 -O1 -g -Isrc -DTCG_NO_DEFAULT_FATAL -Wall -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer' \
  poweraudit
make -C port build/harness/unitdiff
port/build/harness/unitdiff poketcg.gbc poketcg.sym 300
```

To intentionally refresh the inventory after source/card regeneration:

```sh
python3 port/harness/powers_catalog.py --output port/reviews/power-audit/catalog.json
```
