# N64 character style directions · Mark, Dr. Mason, Ronald

Four complete directions, each applied consistently to the three leads, captured
in the real laboratory room, conversation camera, naming portrait and a close
turnaround, from an isolated `DIAGNOSTIC=18` ROM in ares (4 MiB, Tiny3D packed
renderer, native 320×240 RDP frames before VI filtering). The `baseline` option
is the current production model set. Production assets are unchanged;
`validation.json` records the ROM hash, frame list, heap figures and the SHA-256
of every option export. Open `index.html` for everything on one page.

Sources, editable `.blend` files and the rebuild commands are in
`port/characters/experiments/n64-style/`.

## What the directions fix compared with the current models

The current figures are assemblies of many small separate primitives (collar
planes, lapel planes, buttons, pocket, strap and buckle boxes, 6-sided limb
prisms) with per-triangle flat shading, so they read as scattered and faceted.
Every option here is a few continuous shells with smooth baked lighting (flat
only in D, where the facets are the point), and all costume detail is painted
into one 32×32 torso texture and one 32×32 face texture per character, which is
how period N64 characters carried their detail.

| | A · Round chibi | B · Pokémon 3D | C · Ocarina townsfolk | D · Angular |
|---|---|---|---|---|
| Head share of height | ~41% | ~36% | ~33%, visible neck | ~40% |
| Shading | smooth, soft (ambient .62) | smooth (ambient .56) | smooth, stronger key (ambient .48) | flat facets (ambient .60) |
| Mark | big smooth cap, mitten hands, big shoes | cap with bangs, anime eyes with glints | smaller cap, slimmer, nose cone | faceted cap, chamfered torso, box shoes |
| Dr. Mason | cloud of white hair lobes, round glasses | swept white forelock, round glasses | receding, side tufts, comb-over, rectangular glasses, age lines | faceted white helmet and crest |
| Ronald | six large swept spikes, angry chibi eyes | eight medium swept-back spikes, narrow eyes, smirk | six modest spikes, small sharp eyes | sharp wedge spikes, dark angular eyes |
| Triangles (Mark / Mason / Ronald) | 954 / 1004 / 894 | 994 / 946 / 950 | 922 / 870 / 872 | 790 / 836 / 732 |

Triangle counts are before any production decimation; the current cast ceiling
is 906. A and B could drop to that by using 10 head segments instead of 12.

## Reading the captures

- `sheets/view-talk.png` and `sheets/view-room.png`: the production cameras.
  A reads best at distance because the face plane is largest; C is the least
  readable far away, as the earlier period survey predicted for slimmer bodies.
- `sheets/view-turn-*.png`: front, side mid-stride, back, side mid-stride at a
  close camera. Back views show the silhouettes the elevated room camera sees
  most of the time: cap, hair mass, backpack, coat.
- `sheets/view-portrait-*.png`: the exact naming-screen viewport and camera.
- `blender/`: Eevee renders of the same meshes with the same baked colours, for
  inspecting construction at high resolution. These are not emulator output.
