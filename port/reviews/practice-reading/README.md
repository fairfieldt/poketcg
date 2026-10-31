# Tutorial reading panels

The original GBC redraws numbered instructions in its main-area checklist
while Mason's acknowledged speech appears separately. The portable frontend
had treated those redraws as ordinary messages, replacing the current text for
one frame between explanations. They now update a complete persistent
checklist without touching speech or consuming input.

Every explicit Mason explanation now uses the same large paper reading panel,
including short lines such as “Finally, attack your opponent.” The bottom
contains only A/B continue and a page position when needed. Long explanations
paginate locally with fresh presses; they retain the same engine ACK lifetime.
The final “Now, let's play” panel includes the completed action recap before
the actual duel menu. Source turn/retry ordering remains intact.

The shared eight-turn test verifies all numbered redraws use checklist events,
every Mason speech waits for an ACK, the first two steps precede “Finally,”
and the complete list reaches the recap. It also verifies the correct Psychic
Energy advances and a wrong Water Energy restores the original turn. Full
8 KB WRAM plus 128-byte HRAM hashes and request counts are exactly unchanged
from the pre-fix runs: 200 requests / `0d4027b58c8a5c77` for completion, and
63 requests / `14b6a98d0ce934ec` for the deliberate wrong-Energy rewind.

Native host layout checks render all 25 main turn explanations at 320×240 and
480×360 without framebuffer overflow. The long first explanation and short
“Finally” explanation produce byte-identical hint-only bottom panels. Browser
checks use the actual text/card records at 320- and 960-pixel widths, verify
the same large panel bounds, no duplicate bottom speech, recap fit, fresh
presses between pages/lessons, and clearing at a new turn.

See [validation.json](validation.json), [320-pixel Finally](320-finally.png),
and [960-pixel recap](960-recap.png). Those two PNGs are browser fixtures.

The [native screenshots](native/validation.json) capture the actual N64 RDP
framebuffer after the production cached canvas and completed RDP copy:
[long explanation at 320](native/mason-long-320.png),
[Finally at 320](native/mason-finally-320.png),
[recap at 320](native/mason-recap-320.png),
[long explanation at 480](native/mason-long-480.png),
[Finally at 480](native/mason-finally-480.png), and
[recap at 480](native/mason-recap-480.png).

The [native fixture](native-fixture.c) calls the real checklist/speech helpers
with the original text records and renders the production UI/video code in
ares. It is a static presentation fixture; the actual eight-turn script is
covered by the shared engine regression. The same native harness also calls
the combined adventure's real `n64_duel_play` adapter with its engine boundary
wrapped: AI practice deck 0 selects practice presentation, other AI decks select
normal AI mode, and a human opponent selects hotseat. All three cases pass,
and the practice setup bytes remain unchanged.
