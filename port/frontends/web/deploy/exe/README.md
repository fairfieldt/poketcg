# Public exe.dev deployment

Public game: https://poketcg64.exe.xyz/

The dedicated VM serves the full adventure and standalone duel frontend through
one Node process on port 8000, including the same-origin `/ws` online-duel relay.
`poketcg.service` starts on boot and restarts after a process failure. exe.dev
provides the public HTTPS proxy. The home tailnet preview remains independent.

The release packager includes only game HTML, JS, CSS, WASM, models, textures,
audio and the shipped tutorial-complete diary fixture. Development reviews,
captures, source files and personal saves are excluded from the public root.
Diaries remain in each visitor's browser storage. Export/import a cartridge to
move existing browser saves to the new origin.

Publish an updated `dist/` bundle from the repository root:

```sh
python3 port/frontends/web/deploy/exe/publish.py
```

The script verifies every transferred file, installs the locked relay dependency,
then switches `~/poketcg/current` atomically and restarts the service. It retains
older releases and restores the previous release if the health check fails.
Online rooms are in memory and reset when the service restarts.

VM provisioning used:

```sh
ssh exe.dev new --name=poketcg64 --cpu=1 --memory=2GB --disk=10GB --no-email --comment=poketcg64-public-web --json
ssh exe.dev share port poketcg64 8000
ssh exe.dev share set-public poketcg64
```

The VM uses the official Node 24.22.2 Linux x64 distribution under
`/opt/poketcg-node`, checked against the vendor's SHA-256 manifest. No game
secrets or external API credentials are required. Inspect or restart the app:

```sh
ssh poketcg64.exe.xyz 'systemctl status poketcg.service --no-pager'
ssh poketcg64.exe.xyz 'sudo journalctl -u poketcg.service -n50 --no-pager'
ssh poketcg64.exe.xyz 'sudo systemctl restart poketcg.service'
```

Initial release: `20261101T005502Z-dadb309b0f10`, source `dadb309b0f10`.
The service is active and enabled on boot. Anonymous HTTPS checks verified
all 1,596 public files against the release hashes, including WASM and audio
MIME types. The WASM compiled successfully, and two public WSS clients created
and joined a room, exchanged setup and sequenced answers, then left.
Private/development paths returned 404. Local release manifests and upload
logs are retained under `port/frontends/web/build/exe-deploy/`.
