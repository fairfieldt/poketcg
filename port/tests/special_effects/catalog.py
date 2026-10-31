#!/usr/bin/env python3
"""Inventory every non-Power Pokemon attack without changing generated data."""
import collections
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'port/tools'))
from romsym import Rom
from gen_data import parse_text_sources

OUT = ROOT / 'port/reviews/special-effects-audit'


def sources():
    functions = {}
    for path in (ROOT / 'port/src').rglob('*.c'):
        if '/gen/' in str(path):
            continue
        raw = path.read_text()
        # Blank comments and string literals, preserving offsets and newlines.
        code = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',
                      lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), raw,
                      flags=re.S)
        for match in re.finditer(r'\b(\w+)\s*\([^;{}]*\)\s*\{', code):
            name = match[1]
            if name in {'if', 'for', 'while', 'switch'}:
                continue
            at, depth = match.end(), 1
            while depth:
                depth += (code[at] == '{') - (code[at] == '}')
                at += 1
            body = code[match.end():at - 1]
            functions[name] = {
                'file': str(path.relative_to(ROOT)),
                'line': raw.count('\n', 0, match.start()) + 1,
                'calls': sorted(set(re.findall(r'\b(\w+)\s*\(', body)) -
                                {'if', 'for', 'while', 'switch', 'sizeof', 'TRACE_FN'}),
            }
    return functions


def mechanics(description, names, has_effect):
    if not has_effect:
        return ['ordinary_damage']
    text = description.lower()
    fn = ' '.join(names)
    rules = {
        'coin': r'flip|coin|heads|tails',
        'status': r'poison|asleep|paraly|confus',
        'damage_modification': r'plus|minus|times|multipl|more damage|additional damage|damage counters on|energy cards attached',
        'recoil': r'damage to itself|damage to ' + r'(?:pikachu|raichu|magneton|zapdos|arcanine|machoke|rhydon|tauros|chansey|golem|weezing)|also does .+damage to',
        'bench_damage': r'benched|each of your.+bench|each of his.+bench|each of your opponent',
        'healing': r'remove.+damage counters|remove.+damage counter|heal',
        'prevention': r'prevent|reduce.+damage|damage.+reduced|does nothing|half the damage',
        'turn_restriction': r'next turn|can.t attack|can.t retreat|can.t use|cannot use',
        'switch': r'switch|switching',
        'energy_removal': r'discard.+energy|discard.+energies',
        'draw_search': r'draw|search|shuffle|deck',
        'copy_attack': r'choose.+attacks|copy|metronome|mirror move',
        'type_weakness_resistance': r'weakness|resistance|color|type of',
        'evolution': r'evolv|evolution|stage',
        'hand_manipulation': r'hand|return.+cards',
    }
    tags = [tag for tag, pattern in rules.items() if re.search(pattern, text)]
    # Names resolve terse descriptions and bespoke effect records.
    name_rules = {
        'coin': r'50Percent|Multiplier|QuickAttack|Thrash|PinMissile|Fury|Punch|Eggsplosion|Slam|Comet|Jab',
        'status': r'Poison|Paralysis|Confusion|Sleep|Toxic|FoulOdor|FoulGas|Venom|Spore',
        'recoil': r'Recoil|Selfdestruct|DoubleEdge|TakeDown',
        'bench_damage': r'BenchDamage|Gigashock|ChainLightning|Earthquake|Selfdestruct|PealOfThunder|Thunderstorm|BigThunder',
        'healing': r'Leech|MegaDrain|Absorb|Recover|Heal|Rest',
        'prevention': r'Barrier|Withdraw|Stiffen|Agility|FlyEffect|Minimize|Harden|Scrunch|Pounce|Growl|Reflect|Leer|TailWag|DestinyBond',
        'switch': r'Teleport|Lure|Whirlwind|Roar|TerrorStrike|Gust',
        'energy_removal': r'Discard|HyperBeam|HyperBeam|Wildfire|Flamethrower|FireSpin|FireBlast|Ember|SuperEnergyRemoval',
        'draw_search': r'CallForFamily|Sprout|Draw|Fetch|Scavenge|Hatch|MixUp|Mimic|Prophecy',
        'copy_attack': r'Metronome|MirrorMove',
        'type_weakness_resistance': r'Conversion',
        'evolution': r'Devolution',
        'turn_restriction': r'Amnesia|Headache|Smokescreen|SandAttack|Acid|BoneAttack|SwordsDance|FocusEnergy|Leer|TailWag',
        'damage_modification': r'DamageBoost|Multiplier|DamageBonus|WaterGun|HydroPump|Rage|Flail|Boyfriends|DoTheWave|Meditate|Psychic|Flare|RazorWind|SuperFang',
    }
    tags += [tag for tag, pattern in name_rules.items() if re.search(pattern, fn)]
    return sorted(set(tags)) or ['bespoke']


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    rom = Rom()
    constants = {key: int(value, 16) for key, value in re.findall(
        r'#define (\w+) (0x[0-9a-f]+)', (ROOT / 'port/src/gen/constants.h').read_text())}
    names = {int(number, 16): name for name, number in re.findall(
        r'^\s*const\s+(\w+)\s*;\s*\$([0-9a-fA-F]+)',
        (ROOT / 'src/constants/card_constants.asm').read_text(), re.M)}
    texts = parse_text_sources()
    text_order = re.findall(r'^\s*textpointer\s+(\w+)',
                            (ROOT / 'src/text/text_offsets.asm').read_text(), re.M)
    def text(number):
        return texts.get(text_order[number - 1], '') if number else ''

    generated = (ROOT / 'port/src/gen/cards.c').read_text().split('gCardDataAddress')[0]
    records = [bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', row))
               for row in re.findall(r'\{([^{}]+)\}', generated)]
    funcs = sources()
    command_c = (ROOT / 'port/src/gen/effect_commands.c').read_text()
    command_asm = (ROOT / 'src/engine/duel/effect_commands.asm').read_text()
    stages = {v: k for k, v in constants.items() if k.startswith('EFFECTCMDTYPE_')}
    card_bank, card_table = rom.addr('CardPointers')
    command_bank, _ = rom.addr('EffectCommands')
    attacks, power_count, pokemon_count = [], 0, 0
    for cid in range(1, constants['NUM_CARDS'] + 1):
        address = rom.word(card_bank, card_table + cid * 2)
        data = rom.read(card_bank, address, 65)
        if data[0] >= constants['TYPE_ENERGY']:
            continue
        pokemon_count += 1
        assert records[cid] == data, f'Generated card differs: {cid}'
        for slot, off in enumerate((12, 31)):
            word = lambda index: data[index] | data[index + 1] << 8
            if not word(off + 4):
                continue
            category = data[off + 11]
            if category & 0x7f == constants['POKEMON_POWER']:
                power_count += 1
                continue
            effect = word(off + 12)
            commands = []
            label = next((n for n in rom.by_addr.get((command_bank, effect), [])
                          if n.endswith('EffectCommands') and n != 'EffectCommands'), None) if effect else None
            if effect:
                at = effect
                while rom.byte(command_bank, at):
                    stage = stages[rom.byte(command_bank, at)]
                    function = rom.name_at(11, rom.word(command_bank, at + 1))
                    assert function in funcs, f'Missing strong implementation: {function}'
                    commands.append({'stage': stage, 'function': function,
                                     **{k: funcs[function][k] for k in ('file', 'line')}})
                    at += 3
                cbody = re.search(r'struct EffectCommand ' + re.escape(label) + r'\[\] = \{(.*?)\n\};', command_c, re.S)[1]
                c_entries = re.findall(r'\{ (EFFECTCMDTYPE_\w+), (\w+) \}', cbody)
                abody = re.search(r'^' + re.escape(label) + r':\s*\n(.*?)(?=^\w+:|\Z)', command_asm, re.M | re.S)[1]
                a_entries = re.findall(r'dbw (EFFECTCMDTYPE_\w+), (\w+)', abody)
                want = [(c['stage'], c['function']) for c in commands]
                assert c_entries == want == a_entries, f'Command order mismatch: {label}'
            description = '\n'.join(text(word(off + x)) for x in (6, 8) if word(off + x))
            reached = set()
            pending = [c['function'] for c in commands]
            while pending:
                fn = pending.pop()
                if fn in reached:
                    continue
                reached.add(fn)
                pending.extend(x for x in funcs.get(fn, {}).get('calls', []) if x in funcs)
            attacks.append({'card_id': cid, 'card_constant': names[cid],
                'card_label': rom.name_at(card_bank, address), 'card_name': text(word(3)),
                'slot': slot, 'attack_name': text(word(off + 4)), 'description': description,
                'category': category, 'damage': data[off + 10],
                'flags': list(data[off + 14:off + 17]), 'energy_cost_bytes': list(data[off:off + 4]),
                'effect_address': effect, 'effect_list': label, 'commands': commands,
                'mechanics': mechanics(description, [c['function'] for c in commands], bool(effect)),
                'calls_transitive': sorted(reached)})
    summary = {'pokemon_cards': pokemon_count, 'power_entries_excluded': power_count,
        'non_power_attacks': len(attacks), 'attacks_with_effect_chains': sum(bool(a['effect_address']) for a in attacks),
        'distinct_effect_chains': len({a['effect_address'] for a in attacks if a['effect_address']}),
        'effect_functions_including_ai': len({c['function'] for a in attacks for c in a['commands']}),
        'runtime_functions': len({c['function'] for a in attacks for c in a['commands'] if c['stage'] not in
            {'EFFECTCMDTYPE_AI', 'EFFECTCMDTYPE_AI_SELECTION', 'EFFECTCMDTYPE_AI_SWITCH_DEFENDING_PKMN'}}),
        'mechanic_tags': dict(collections.Counter(tag for a in attacks for tag in a['mechanics'])),
        'stages': dict(collections.Counter(c['stage'] for a in attacks for c in a['commands']))}
    result = {'summary': summary, 'checks': {'all_pokemon_card_bytes_match_rom': True,
        'all_attack_chains_match_generated_c_and_original_asm_in_order': True,
        'all_referenced_functions_have_strong_c_implementations': True},
        'rom_sha256': hashlib.sha256(rom.data).hexdigest(),
        'classification_note': 'Mechanic tags are descriptive indexing, not proof of branch coverage. Transitive calls are conservatively parsed direct C calls.',
        'attacks': attacks}
    (OUT / 'inventory.json').write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
