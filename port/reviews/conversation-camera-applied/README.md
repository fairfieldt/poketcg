# Applied conversation camera

The shared portable-C presentation now owns an explicit NPC conversation override. Named speech/choice callbacks begin and end it on N64 and web. It fits both upper bodies at 20 degrees, starts 6.8 horizontal units from the midpoint, expands for separated actors, and favors a speaker-facing side inside the room. Source furniture footprints influence side selection. Invalid, hidden, distant, prop and elevated/reclining speakers retain the wider view. Story positions, headings, events and diary layout remain unchanged.

The browser advances only the camera while Asyncify waits in a modal; scripted frame callbacks retain actor stepping. The N64 uses its existing frame callbacks. Closing dialogue returns smoothly to the map/intro camera.

DIAGNOSTIC=13 / HIGH_RES=1 runs the original opening through Mason and Sam's practice-duel request, then tests Ronald's source Psychic Club lobby position and the entrance clerk. Completed production frames are held for 24 presentations to acquire stable VI output. This freeze affects only the diagnostic capture, not gameplay. Both RAM configurations use the same diagnostic ROM.

All 12 original encoder images are compared against exact Program::video callback RGB bytes. Captures require the matching native header, six subsequent valid callbacks and six stable callbacks. Native framebuffer PNGs are separate. Run build_gallery.py after capturing both cases with ../expansion-pak/capture.py.

Host tests cover 11 directional/source/wall compositions, dialogue clearance, stable bubbles, exit restoration and fallback. The actual browser source opening checks Mason and Sam while suspended, unchanged canonical actors/coordinates, and continued progression into the duel. Existing movement tests verify original tutorial timing and continuous ordinary movement.
