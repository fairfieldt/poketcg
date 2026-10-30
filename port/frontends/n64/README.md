# Nintendo 64 frontend

A libdragon frontend for the portable duel engine (`port/src`, interface in
`port/src/tcg.h`). It builds `tcgn64.z64`.

## Building

You need the libdragon toolchain with libdragon and its tools (n64.mk,
mksprite, mkdfs, n64tool) installed in `$N64_INST`. The Makefile defaults to
`/home/user/tools/n64/opt/libdragon`.

    make             # tcgn64.z64
    make hosttest    # the UI on the host, against mock/libdragon.h (scripts in tests/)
    make endiancheck # the engine natively and as big-endian MIPS under qemu-mips
    make clean

To set the toolchain up from scratch:

1. Download `gcc-toolchain-mips64-x86_64.deb` from libdragon's
   `toolchain-continuous-prerelease` release and run
   `dpkg -x gcc-toolchain-mips64-x86_64.deb /home/user/tools/n64`.
2. Clone libdragon.
3. In the libdragon tree, with `N64_INST` set, run
   `make install-mk && make libdragon tools && make install tools-install`.

The engine sources are compiled from `port/src` like this:

- `src/*.c` and `src/gen/*.c` are always built.
- `src/effects/*.c` and `src/ai/*.c` are optional. `tools/select_optional.sh`
  leaves out the files that do not compile or link yet. The weak stubs stand
  in for them.
- The result is printed as `[OPT]` lines and shown on the title screen.

The card art comes from `src/gfx/cards/*.png` of the disassembly. It is
converted to RGBA16 sprites with mksprite and stored in the ROM filesystem.

## Playing

The title screen has these choices:

- **Continue Duel** — shown when a saved duel exists.
- **Mode**:
  - VS CPU (the engine's AI).
  - VS random bot (a random player run by the frontend).
  - 2 players hot-seat (the second player uses controller 2, or controller 1
    when no controller 2 is plugged in).
  - Practice with Sam (the original's practice duel with Dr. Mason's
    instructions: 2 prizes, the practice decks).
- **Your deck** and **Opponent** — any of the pre-constructed decks.
- **Prizes**.

The last choices are remembered.

Controls: A select/confirm, B cancel, D-pad or stick move, START card
details, Z the check screen (both play areas and discard piles), L/R page.

## Saving

The engine calls `TcgHost.save` whenever the original would write the duel
to SRAM. The save is 1660 bytes. The frontend keeps the latest one in RAM. At
each turn boundary (`TCG_EV_TURN`) it hands the save to the save store, but
only if it changed. When a duel is over, the save is deleted. A small file
`duelmode` records how the two sides are controlled (CPU, bot or hot-seat),
so that "Continue Duel" maps the controllers the same way. Bot and hot-seat
sides are both `DUELIST_TYPE_PLAYER` in the engine's save, so the save alone
cannot tell them apart.

### Why EEPROM

The store is `save_store_n64.c`, the N64 implementation of
`../common/save_store.h`. It uses the cartridge's **16 Kbit EEPROM** through
libdragon's EEPROM API (`eeprom.h`). The ROM header declares this save type
(`N64_ROM_SAVETYPE = eeprom16k`), which emulators and flashcarts read.

- **EEPROM is the save type this libdragon fully supports.** It has
  detection, block read/write, and the eepromfs filesystem. This libdragon
  version has no SRAM or FlashRAM API. Using SRAM would mean programming PI
  DMA to the cartridge domain-2 address and its timings by hand. FlashRAM
  would also need its command protocol, with no library help.
- **A 4 Kbit EEPROM is too small.** It has 512 bytes and the duel save is
  1660 bytes. 16 Kbit gives 2048 bytes. Our layout uses 215 of the 256
  blocks: a signature block, then for each file a header block followed by
  its data blocks.
- **Writes are slow.** An EEPROM block (8 bytes) takes about 15 ms to write,
  and each block only survives a limited number of writes. libdragon's
  `eepfs_write` always rewrites a whole file: 208 blocks, about 3.1 s, for
  the duel save. The duel save actually changes in about 25 blocks per turn
  on average (measured over random duels). So the store works this way:
  - It keeps a RAM mirror of the EEPROM and writes back **only the blocks
    that changed**.
  - It writes **one block per frame** from the render loop
    (`save_store_poll`). `eeprom_write` returns at once and the chip's busy
    time overlaps the next frame, so the game never stalls.
  - "saving." blinks in the bottom margin while blocks are pending.
- **A save never comes back half-written.** A file's data blocks are written
  first and its header (size, CRC-32) last. If the power goes off in the
  middle, the data does not match the header's CRC, so the file reads as
  missing. The save is lost rather than mixed. This window is the write-back
  after each turn boundary: about 25 frames on average. The first save of a
  duel writes all 209 blocks, which takes about 3.5 s.
- **Saves happen at turn boundaries only.** Turning the console off in the
  middle of a turn resumes from the last turn boundary. That state is the
  original's last duel-menu save from before that point.
- **Wear.** A block that changes every turn (the turn counter, the random
  number generator) is written about once per turn: 70 times in a 71-turn
  duel in the host tests. EEPROMs typically last 100,000 or more writes per
  block, which is over a thousand long duels.

## Testing on the host

`make hosttest` builds `tcgn64.c` and `save_store_n64.c` against
`mock/libdragon.h`. The mock:

- draws into a memory framebuffer with libdragon's semantics, and aborts on
  out-of-bounds drawing or non-ASCII text;
- checks that the data cache is written back before each frame is shown;
- feeds scripted or seeded-random controller input;
- emulates a file-backed EEPROM. It can `reboot` (power-cycle) or
  `poweroff` at any frame.

Screenshots and logs go to `build/host/`.

| Script | What it covers |
|---|---|
| `vs_bot` | play against the random bot |
| `tour_bot` | a whole duel against the random bot |
| `vs_ai` | play against the engine AI |
| `hotseat` | a two-controller duel |
| `hotseat_ports` | controller routing in hot-seat |
| `ui_tour` | title screen, check screen and card pages |
| `widgets` | synthetic requests for the screens random play rarely reaches (`TCGN64_SELFTEST=1`, host builds only) |
| `practice` | the practice duel with Sam |
| `save_continue` | save, power off, continue; a power-off during the write-back; the save deleted when the duel ends |

`make endiancheck` plays 36 duels and continues 24 of them from a save. It
does this natively and as a big-endian MIPS32 Linux program under
qemu-mips. It then compares a digest of every event, request, answer and save
image, and of the final engine memory.

## Files

| File | Contents |
|---|---|
| `tcgn64.c` | the frontend |
| `save_store_n64.c`, `save_store_n64.h` | save_store.h on the EEPROM |
| `mock/` | the host mock of libdragon |
| `tools/cardart.awk` | maps each card's `gfx` pointer to its picture (`src/gfx.asm`) |
| `tools/select_optional.sh` | chooses which optional engine files to link |
| `tools/endian_check.c`, `tools/endian_check.ld` | the byte order check |
| `tests/*.txt` | host test scripts |

## Limitations

- Not run on a console or in an emulator yet. Only the build and the host
  mock exercise it.
- No sound.
- The 8x8 font is ASCII only.
- The board is always seen from player 1's side.
- No network play.
