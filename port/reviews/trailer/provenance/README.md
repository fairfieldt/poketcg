# Capture route provenance

[routes.json](routes.json) records all four private ROM hashes, native flow
gate paths and the legal Ronald checkpoint SHA256. The canonical player name
is **Mark**; Ronald's displayed name is **Ronald**.

Intro runs the real new-game source through Sam's first practice-duel request.
Ronald continues an untouched, engine-generated mid-duel save and uses real
controller input/native UI for the attack, knockout, Prize and replacement.
Club footage uses private post-tutorial room warps followed by legal player
walking. Ending declares a private Amy win, runs her actual reward script and
Water Medal ceremony, then uses a Hall of Honor progression warp before the
actual title. It is a montage, not one continuous campaign run.

The controller driver supplies fresh automatic ACKs, normally three seconds
per story page. Its eight-second silent black preroll is trimmed from the
edit; the first source world movement has an `INTRO_RUN` marker. Developer
markers/counters are not drawn over gameplay. All profiles and saves are
private to the capture ROMs.

The separate capture agent records actual CRT Guest Advanced shader output
and private game audio. Local native flow proofs do not substitute for that
final output capture.
