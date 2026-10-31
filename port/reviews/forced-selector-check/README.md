# Z opens Check during Pokémon selection

The N64 forced replacement selector now opens Check with Z and returns to the same selected Pokémon. START still opens that Pokémon’s card. Pressing Z and A together opens Check without answering the pending selector. During a practice lesson, Z also opens Check and returns to the same lesson without acknowledging it.

The short selector hints retain the hotseat controller label. No engine rule, request, answer, RNG or forced-selection behavior changed.

Validation: `make -C port/frontends/n64 checktest inputtest acktest -j6` passes. The new fixture seeds Sam Turn 4’s post-KO state (Seaking knocked out, Staryu and Drowzee on the Bench), calls the real `OpenPlayAreaScreenForSelection`, and repeats it with P2 owning the request. Both controller cases inspect Drowzee with START, return through Check with Z/B, and select Staryu only with a later A. The same actions cover the bench-menu selector. A fifth case uses the actual `SamTurn4DrMason2PracticeDuelText` lesson and requires a later A to acknowledge it. Each case matches the headless engine’s complete WRAM, HRAM and event/request/answer digest, including RNG bytes. All five cases pass at 320×240 and 480×360. The physical joypad Z test verifies a one-VI tap survives buffering and is consumed once. Existing drawn-card/placement fresh-confirmation regressions also pass at both resolutions.

The PNGs are host-mock renderer captures, not emulator VI or hardware captures. They show the restored selector, START card inspection and resumed practice lesson in both resolutions. No additional native timing run was needed for this input-only change.
