# Pokemon Card GB - map -> 3D room guide

Companion to the per-room `*.txt` briefs that sit next to each PNG in this
folder.  Everything here was derived from the rendered maps (they are produced
from the ROM by `../tools/render_maps.py`) and from the tile/permission/warp
data in `../../src`.

* One map tile = 8 x 8 px in the PNGs.
* Suggested working unit: **1 tile = 1 unit**.  If you want metres, 1 unit
  ~ 0.5 m is a good game-y scale (a 20 x 18 foyer is then 10 x 9 m).
* A character sprite is 16 x 16 px = **2 x 2 tiles**, so a person is 2 units
  tall.  For N64-era interiors, build the walls 4-5 units tall; that gives the
  slightly tall, boxy rooms the consoles of that era always showed.

## How to read the top-down maps as 3D rooms

The maps are top-down orthographic with a fake elevation:

* the **top band** of every map is the **north wall drawn in elevation**.  All
  wall decoration (framed pictures, signs, shelves, banners, friezes) lives
  there and is drawn front-on.  In 3D it becomes a real wall with those objects
  standing against it or hung on it.
* the **left, right and bottom bands** are the same wall seen from above.  They
  are 1-2 tiles thick and carry no decoration except where a door cuts through.
* objects drawn on the floor near the top band are floor-standing props seen
  from the front (counters, shelves, machines).
* objects drawn in the middle of a room are seen from a high camera: you get a
  bit of top surface plus a bit of front face.  Model them as solid props and
  let the camera do the rest.
* the outermost black row/column of a map is **not geometry**.  It is the
  "outside the room" padding the renderer uses.  Ignore it.

Coordinate convention used by every brief: `x` grows east (right), `y` grows
south (down), `x0,y0` is the top-left pixel of the image = the north-west
corner of the room.  `x a-b y c-d` means the tile rectangle whose inclusive
bounds are those coordinates.  "North" = top of the image.

## Level of detail that matters

These rooms were 20x18 to 32x32 tiles of 4-colour tile art.  The N64-era
translation is: low-poly boxes with flat or 2-bit-checker textures, no
transparency, no light maps except a flat per-room tint, one texture per
material, and a fixed camera per room.  Reuse geometry aggressively: the
furniture below is the same mesh recoloured.

## The three shared room shells

### 1. Club entrance foyer - `*_club_entrance.png`, `challenge_hall_entrance.png`

20 x 18 tiles.  **All nine are the same room**: `TILEMAP_*_CLUB_ENTRANCE_CGB`
all reference the same `TILESET_CLUB_ENTRANCE`, and comparing the tilemaps
pairwise gives 344-356 identical tiles out of 360.  The only difference is the
4 x 4 emblem plaque in the middle of the floor (16 tiles) - and the Challenge
Hall has no plaque at all.  Palettes 0,1,2,4,5,6,7 are byte-identical across
all nine; only palette 3 (the emblem's) is per-club.

Shared geometry:

| part | tiles |
|---|---|
| walkable floor | x2-17, y3-13 (16 x 11) |
| north wall band | y0-2, decorated in elevation |
| south wall band | y14-17 (same decoration mirrored + 1 black edge row) |
| west wall band | x0-1 |
| east wall band | x18-19 |
| north door (to club room) | x8-11 at y0, with a red runner x8-11 y0-3 |
| south door (to the street) | x8-11 at y17, no runner |
| west door (to club lobby) | x0 at y6-9, with a red rug x0-2 y7-10 |
| floor emblem plaque | x8-11, y6-9 (4 x 4), **the only per-club piece** |
| framed pictures | x3-4 and x15-16 at y1-2 (north wall), mirrored at y15-16 |
| wainscot panels | x2-6 and x13-17 at y1-2 and y15-16, gold/white checker |
| potted palm | x16-17, y10-13 (south-east corner) |

So: build one foyer mesh and swap one 4x4 decal plus its 4-colour palette.

### 2. Club lobby - `*_club_lobby.png`

28 x 26 tiles.  **All nine lobbies are literally identical** - they share
`TILEMAP_CLUB_LOBBY_CGB`, `TILESET_CLUB_LOBBY` *and* `PALETTE_CLUB_LOBBY`,
and pairwise comparison gives 728/728 identical tiles.  Nothing about the
geometry, the art or the colours changes per club.  Only the NPC list and the
warp destinations differ, so a lobby `.txt` is a shell around the shared
description in `club_lobby_3d.txt`.

### 3. Club rooms - `*_club.png`

These are **not** shared: every club has its own tileset, its own size
(24x18 to 32x32) and its own layout.  What they do share is a recipe:

* one big symmetric hall, mirror-symmetric about the room's vertical axis
  (Fighting and Grass are the exceptions - they are asymmetric);
* a border wall band 2 tiles thick on all sides, drawn in the club's theme
  colours, with a decorative frieze on the north band;
* one strong axis: a runner/carpet/aisle down the centre from the north wall
  to the south door;
* two mirrored columns of theme props (statues, plants, topiaries, machines,
  pedestals, display cases) spaced at a regular interval along that axis;
* the player enters at the south door, and the room is a dead end: the only
  warp in a club room leads back to its own entrance.

Model each club as its own mesh, but reuse the runner mesh, the pot/plinth
meshes and the wall-band shader.

## Theme colours

The club theme palettes (this is what changes between rooms that share
geometry):

| club | accent colours (hex) |
|---|---|
| Fighting | `#fff7b5` `#e76300` `#6b2900` - orange/rust |
| Rock | `#decebd` `#b58463` `#734221` - stone grey-brown |
| Water | `#ffffff` `#00fff7` `#0073ff` - cyan/blue |
| Lightning | `#ffffff` `#ffff00` `#ffa500` - yellow/amber |
| Grass | `#ffffff` `#9cff29` `#009c21` - yellow-green/green |
| Psychic | `#ffffff` `#ff29ff` `#a500ff` - magenta/violet |
| Science | `#ffffff` `#00ff31` `#00bd21` - pure green |
| Fire | `#ffffff` `#ffa500` `#ff0000` `#420000` - orange/red |
| Challenge Hall | see `challenge_hall.txt` |
