# Steam Machine playtesting

The Steam machine is reachable as `deck@steamdeck.tdf.sh` (`100.74.1.31`), with
Tom's existing SSH key. LAN fallback: `deck@192.168.0.233`. SteamOS runs ares v148
from Flathub. Steam entries are `Pokemon TCG 64` and
`Pokemon TCG 64 (Expansion Pak)`; `Nintendo 64 (ares)` opens the emulator.

Publish an existing build from pinkerton:

```sh
~/dev/homelab/scripts/publish-n64 \
  ~/dev/poketcg/port/frontends/n64/adventure/poketcg-adventure.z64
~/dev/homelab/scripts/publish-n64 --variant expansion \
  ~/dev/poketcg/port/frontends/n64/adventure/poketcg-expansion.z64
```

The default variant is standard; specify `--variant expansion` for the other ROM.
`--host deck@192.168.0.233` uses the LAN fallback. The receiver is installed at
`/home/deck/.local/bin/receive-n64-build` on the machine, requires no sudo, and
accepts a variant and SHA-256 with the ROM on stdin. The client snapshots the
built ROM before sending, so a concurrent build cannot modify an upload.

Updates replace the selected ROM atomically, retain archived releases, and do
not restart the emulator. Quit the game and launch it again to load an update.
Saves are separate from the ROM releases; publishing never deletes saves.
Do not ship debug/diagnostic ROMs under the normal game entry unless requested.

Host setup and recovery: `~/dev/homelab/hosts/steamdeck/README.md`.
