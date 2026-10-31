#!/usr/bin/env python3
"""Check every Power card/command chain against original assembly; emit inventory."""
import argparse
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
MANUAL = {"SolarPowerName", "EnergyTransName", "HealName", "ShiftName", "CowardiceName",
          "PeekName", "DamageSwapName", "StrangeBehaviorName", "CurseName", "StepInName"}
TRIGGER = {"FiregiverName", "QuickfreezeName", "PealOfThunderName", "HealingWindName"}


def catalog():
    original = (ROOT / "src/data/cards.asm").read_text()
    labels = list(re.finditer(r"^([A-Za-z0-9_]+Card):\n", original, re.M))
    source = {}
    for i, label in enumerate(labels):
        block = original[label.end():labels[i+1].start() if i+1 < len(labels) else len(original)]
        if "db POKEMON_POWER" not in block:
            continue
        before, after = block.split("db POKEMON_POWER", 1)
        names = re.findall(r"\btx (\w+) ; name", before)
        source[label.group(1)] = {
            "card": re.findall(r"^\s*db ([A-Z][A-Z0-9_]+)\s*$", block, re.M)[0],
            "name": names[-1],
            "chain": re.search(r"dw (\w+) ; effect commands", after).group(1),
            "source": f"src/data/cards.asm:{original[:label.start()].count(chr(10))+1}",
        }
    asm = (ROOT / "src/engine/duel/effect_commands.asm").read_text()
    c = (ROOT / "port/src/gen/effect_commands.c").read_text()
    addresses = {int(address, 16): name for address, name in
                 re.findall(r'\{ 0x([0-9a-f]+), "(\w+)", \w+ \}', c)}
    constants = dict(re.findall(r"^#define (\w+) (0x[0-9a-fA-F]+|[0-9]+)$",
                               (ROOT / "port/src/gen/constants.h").read_text(), re.M))
    category = int(constants["POKEMON_POWER"], 0)
    generated = (ROOT / "port/src/gen/cards.c").read_text()
    rows = re.findall(r"/\* \$([0-9a-f]+) (\w+) \((\w+)\).*?\*/\s*\{([^}]+)\}", generated)
    found = []
    for card_id, constant, label, raw in rows:
        data = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-f]+)", raw))
        if data[0] >= 8:
            continue
        for slot, offset in enumerate((12, 31)):
            if data[offset+11] != category:
                continue
            expected = source[label]
            assert expected["card"] == constant
            assert int(constants[constant], 0) == int(card_id, 16) == data[7]
            address = int.from_bytes(data[offset+12:offset+14], "little")
            chain = addresses[address]
            assert chain == expected["chain"], (constant, chain, expected)
            block = re.search(rf"^{chain}:\n(.*?)(?=^\w+:|\Z)", asm, re.M | re.S).group(1)
            original_commands = re.findall(r"dbw (\w+), (\w+)", block)
            block = re.search(rf"static const struct EffectCommand {chain}\[\] = \{{(.*?)\n\}};", c, re.S).group(1)
            generated_commands = re.findall(r"\{ (EFFECTCMDTYPE_\w+), (\w+) \}", block)
            assert generated_commands == original_commands, (chain, generated_commands, original_commands)
            found.append({**expected, "card_id": int(card_id, 16), "attack_slot": slot,
                          "kind": "manual" if expected["name"] in MANUAL else
                                  "on-play" if expected["name"] in TRIGGER else "passive",
                          "command_address": f"0x{address:04x}",
                          "commands": [{"phase": phase, "function": function}
                                       for phase, function in original_commands]})
    assert len(found) == len(source) == 26
    assert len({row["chain"] for row in found}) == 26
    assert sum(row["kind"] == "manual" for row in found) == 10
    assert sum(row["kind"] == "passive" for row in found) == 12
    assert sum(row["kind"] == "on-play" for row in found) == 4
    return {"power_card_variants": 26, "unique_chains": 26,
            "manual": 10, "passive": 12, "on_play": 4, "powers": found}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", type=Path)
    args = parser.parse_args()
    inventory = catalog()
    if args.check:
        assert json.loads(args.check.read_text()) == inventory, "Power catalog is stale; regenerate with --output"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(inventory, indent=2) + "\n")
    print("POWER CATALOG PASS: all26 card variants/26 unique chains match original assembly;10 manual,12 passive,4 on-play")
