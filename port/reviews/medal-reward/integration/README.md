# Medal reward integration

The portable story already grants the medal correctly. Its medal feature callback receives the original ownership event ID after the ownership flag and milestone mail have been updated. The previous native callback supplied a text message without the medal artwork.

| Feature argument | Medal / leader | Owned bit | Original tilemap | CGB palette slot |
|---|---|---|---|---|
| 8 | Grass / Nikki | 0x80 | 0x3e | 1 |
| 9 | Science / Rick | 0x40 | 0x3f | 1 |
| 10 | Fire / Ken | 0x20 | 0x40 | 2 |
| 11 | Water / Amy | 0x10 | 0x41 | 3 |
| 12 | Lightning / Isaac | 0x08 | 0x42 | 4 |
| 13 | Psychic / Murray | 0x04 | 0x45 | 6 |
| 14 | Rock / Gene | 0x02 | 0x44 | 5 |
| 15 | Fighting / Mitch | 0x01 | 0x43 | 5 |

Use `argument - EVENT_BEAT_NIKKI` for the display index and `0x80 >> index` for its bit. Tilemap IDs cannot be calculated by adding that index: Psychic and Fighting would be interchanged. The source PNG contains eight 24×24 symbols in the event order, while `MedalGfx` repacks its 72 tiles for the original tilemaps. `MedalsPal` supplies six four-color palettes loaded starting at CGB slot 1; slot 0 is the existing menu palette.

The original `ShowMedalReceivedScreen` pauses the room song, stops the active song, draws the collected medals in a four-by-two grid, supplies the winning medal name, flashes white for one source frame, and starts `MUSIC_MEDAL` (29). Its flash loop advances 225 source frames, alternating the awarded medal at 16-frame boundaries, before the source “Won the … Medal!” text is shown. It then waits for the text acknowledgment and any remaining fanfare before resuming the paused song. At the port's 30 FPS target, preserve the approximately 3.75-second flash-loop duration with elapsed time or original 60 Hz units rather than counting 225 native game frames. The caption's map-name labels are simply “Water”, “Grass”, etc., without “Club”.

Amy's actual first-win route is:

1. Reload the post-duel NPCs and clear the transient game-event marker.
2. Print her victory and medal-introduction messages.
3. Set `EVENT_BEAT_AMY`, update the medal count/milestone mail, and invoke the medal feature with argument 11. The Water bit 0x10 is already owned here.
4. Record master win code 3, print her follow-up, grant two Laboratory Water boosters, and print the final reward message.
5. Return Amy to her resting location when the canonical standing encounter was used.

The ceremony must acknowledge its own final screen, then return to the story without changing events, card rewards, player/NPC state, or user saves. Keep the award's visible identity consistent across its name, artwork, highlighted collection slot and owned bit. Native frame/buffer/audio/input checks belong to the renderer's separate gate.

## Host validation

The new `port/overworld/tests/medal_awards.c` passes:

- All eight real `Script_Beat*` reward scripts: exact event ID, already-owned bit/count, ceremony before follow-up rewards, source booster identity, and no repeated ceremony on rematches.
- Cumulative awards: all eight bits, counts, master-record order, and the original 3/7/8-medal PC-mail order.
- In-memory save encode/decode roundtrips preserving flags, count, master records and RNG.
- The actual `ow_finish_duel` → Water Club after-duel handler → Amy reward path from player position 20,6, including her standing post-duel position 20,4 and exact seven-step text/medal/booster sequence.

[medal-awards.log](medal-awards.log) is the clean final run. Existing [story tests](story.log) pass all 714 NPC-state encounters with zero failures. The existing [campaign](campaign.log) reaches all eight medals, the Grand Masters and Ronald, legendary rewards and credits: 73 duels and 1,337 cards.

No production code, shared Makefile, user save or emulator was changed by this integration audit. Reproduce the new gate from the repository root:

```sh
mkdir -p port/overworld/build/medal-audit
cc -std=c11 -Wall -Wextra -Werror -g -Iport/overworld -Iport/src \
  port/overworld/tests/medal_awards.c port/overworld/overworld.c \
  port/overworld/gen/data.c -o port/overworld/build/medal-audit/medal_awards
port/overworld/build/medal-audit/medal_awards
```

Source authority: `src/engine/menus/medal.asm`, `print_stats.asm`, `init_menu.asm`; `src/scripts/water_club.asm` and the other club reward scripts; `src/data/palettes2.asm`; `src/gfx.asm`; portable opcode 0x2a in `port/overworld/overworld.c`.
