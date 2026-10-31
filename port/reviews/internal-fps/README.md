Internal game FPS counter
=========================

The ROM draws `Game N` in its upper-left corner. It counts newly completed
frame presentations over N64 monotonic time, with a half-second sample window.
Repeated VI scanouts do not count. This is independent of the host Output FPS
counter. Startup shows `Game --` until the first sample completes.

The badge is a small cached RGBA16 surface. It is copied with RDP into a
completed, still-owned framebuffer and fenced before display_show. It never
CPU-writes a live RDP framebuffer. Both 320x240 and 480x360 use native font masks.
The badge uses logical x4,y2,width64,height14; text begins at x8,y4. At centered
4:3 on a 3840x2160 display, its backing spans x516..1092,y18..144, and text
begins at x552,y36. The external counter is coordinated separately with the
Steam Machine task; its launcher changes apply only at next launch.

Validation
----------

The sampler test checks 30/15/60Hz intervals, first-window counting and a clock
already beyond 32-bit rollover. Native ares captures in 320/ and 480/ verify the
badge is present through the real VI path in world, naming and dialogue views.
Capture provenance includes ROM/configuration hashes and exact callback RGB
verification. Native capture dumping deliberately stalls the game; these
screenshots are appearance checks, not performance measurements.

The badge does not exist in the web renderer. The N64 adventure entry points
(intro, naming, world, dialogue, transitions and duel UI) share one presentation
function, so a slow duel reports its game rate rather than the 60Hz host output.
