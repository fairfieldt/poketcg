# Pokémon Power selection regression

The Power picker now takes its first slot from the current menu parameters instead of a stale Bench-only flag. Dedicated Power, Grass Energy donor/recipient and confirmation prompts replace unrelated earlier dialogue while preserving the duel log. The Energy Trans rules and transfer logic remain unchanged.

The focused N64 host regression seeds the old Retreat message and `wExcludeArenaPokemon=1`, then runs the real Venusaur Lv67 Power picker followed by `UseAttackOrPokemonPower` and the Energy Trans effect. Both controller owners pass at 320×240 and 480×360.

The sixteen cases cover a successful transfer, cancelling the donor, cancelling the recipient, retrying a donor without Grass Energy, cancelling the Power picker, no Grass Energy anywhere in play, Sleep blocking the Active Venusaur, and Muk’s Toxic Gas. Each runs once for P1 and once for P2. START proves that the initially displayed Venusaur is slot 0 and that the selected donor and recipient are slots 1 and 2. A successful transfer moves Grass Energy deck index 6 from slot 1 to slot 2; Grass Energy 7 and Water Energy 8 remain on slot 1. Cancelled and blocked flows move no energy.

Every interactive run matches the headless run’s complete WRAM, HRAM and event/request/answer digest, including RNG state. This fixture exercises the real Power/confirmation/transfer selectors. Procedure and error ACKs are answered directly by the test callback, so this is not an end-to-end timing or text-pagination test.

The contextual footer tests compare Power choice, Energy Trans donor, recipient and the use-Power confirmation against the same rendering with an empty log. All pixels match and the saved log remains unchanged. Normal footers still display the log.

Validation: `make -C port/frontends/n64 powertest checktest -j6` passes. The seven earlier forced-selector/evolution cases also remain green.

The eight PNGs are host-mock CPU renderer captures at the two native display sizes, with placeholder card art. They are not emulator VI or physical-console captures. No native timing run or production ROM build was performed for these tests.
