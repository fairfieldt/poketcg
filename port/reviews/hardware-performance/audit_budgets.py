"""Read-only arithmetic/ELF audit. Writes budgets.json and felt-disassembly.txt."""
import hashlib
import json
import re
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
N64 = ROOT / "port/frontends/n64/adventure"
TOOLS = Path("/opt/libdragon/bin")

def command(*args):
    return subprocess.check_output([str(a) for a in args], text=True)

def record_file(path):
    data = path.read_bytes()
    return {"path": str(path.relative_to(ROOT)), "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}

roms = [record_file(N64 / name) for name in
        ("poketcg-adventure.z64", "poketcg-expansion.z64")]
elf = N64 / "build/poketcg-adventure.elf"
sections = {}
for line in command(TOOLS / "mips64-elf-size", "-A", elf).splitlines():
    match = re.match(r"(\.[\w.]+)\s+(\d+)\s+", line)
    if match and not match[1].startswith(".debug"):
        sections[match[1]] = int(match[2])

disassembly = command(TOOLS / "mips64-elf-objdump", "-d",
                      "--disassemble=draw_board", elf)
lines = disassembly.splitlines()
div_lines = [i for i, line in enumerate(lines) if re.search(r"\bdiv\s", line)]
assert len(div_lines) == 2, "Inspect a changed binary before reusing the old formula"
first, last = max(0, min(div_lines)-5), min(len(lines), max(div_lines)+12)
(HERE / "felt-disassembly.txt").write_text(
    json.dumps(record_file(elf), indent=2) + "\n\n" + "\n".join(lines[first:last]) + "\n")

modes = []
for width, height in ((320, 240), (480, 360)):
    # Exact positive logical-to-physical coordinate rounding in tcgn64.c.
    def x(value): return (value * width + 160) // 320
    def y(value): return (value * height + 120) // 240
    felt_pixels = (x(310)-x(10)) * (y(190)-y(8))
    divisions = felt_pixels * len(div_lines)
    stalls = divisions * 37
    modes.append({"width": width, "height": height,
                  "frame_pixels": width*height,
                  "one_rgba16_surface_bytes": width*height*2,
                  "five_persistent_surfaces_bytes": width*height*2*5,
                  "seven_surfaces_naming_bytes": width*height*2*7,
                  "felt_pixels_per_board_frame": felt_pixels,
                  "felt_divisions_per_board_frame": divisions,
                  "felt_DIV_stall_CPU_cycles": stalls,
                  "felt_DIV_stalls_ms_at_93_75MHz": stalls/93750,
                  "board_fps_ceiling_from_DIV_stalls_alone": 93750000/stalls,
                  "30fps_one_surface_payload_MB_per_second": width*height*2*30/1000000})

assets = {}
for category in ("audio", "cards", "intro", "world"):
    files = [p for p in (N64 / "build/filesystem" / category).rglob("*") if p.is_file()]
    assets[category] = {"files": len(files), "payload_bytes": sum(p.stat().st_size for p in files)}

result = {"repository_commit": command("git", "-C", ROOT, "rev-parse", "HEAD").strip(),
          "note": "Working tree may include newer camera edits; ROM and ELF hashes identify audited binaries.",
          "roms": roms, "elf": record_file(elf), "elf_sections": sections,
          "modes": modes, "filesystem_payloads": assets,
          "primary_DIV_source": "https://datasheets.chipdb.org/NEC/Vr-Series/Vr43xx/U10504EJ7V0UMJ1.pdf",
          "DIV_source_location": "NEC VR4300 User's Manual, Table 3-12, printed page 76: DIV stalls 37 PCycles",
          "clock_source": "https://ultra64.ca/files/documentation/online-manuals/man-v5-1/kantan/step1/2/2_2.htm"}
(HERE / "budgets.json").write_text(json.dumps(result, indent=2)+"\n")
print(json.dumps({"modes": modes, "roms": roms}, indent=2))
