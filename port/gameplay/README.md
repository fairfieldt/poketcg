# Portable collection, decks, rewards, mail and peer transactions

Compile `gameplay.c` and `gen/data.c` with a C11 compiler. `gameplay.h` is the
public interface. No libdragon, graphics, Game Boy CPU, or runtime ROM loading
is required. `python3 gen_data.py` regenerates tables from the assembled
`poketcg.gbc` and `.sym`; the generated C is checked in.

`GpState` keeps the original distinction between free cards, cards committed
to four constructed decks, and sixty saved deck recipes. Collection byte
`0x80` means never acquired. Zero means previously acquired but no free copies.
`gp_card_count` includes built decks; `gp_collection_count` excludes them.
All four built decks and collection together cap a card at99 copies.

`gp_give_starter` supplies the selected60-card deck and30 extra cards. Preset
deck table indexes are original `DeckPointers` indexes, **two greater than
`*_DECK_ID`**. Recipes and card metadata retain original text IDs. `GpDeck.name`
contains a custom printable frontend name; when empty, use `name_text`.

`gp_open_booster` reproduces the original ten-card draw algorithm and adds the
cards to the collection. `gp_give_mail` retains a unique mail ID with unopened
bit; `gp_open_mail` opens it once and gives its one/two boosters. The exported
mail text table contains both original page text IDs.

Challenge Machine APIs preserve the five-opponent sequence, results, streak,
record, and record holder. Set `challenge.in_duel` before persisting an ongoing
duel; on continuation apply exactly one result through `gp_challenge_result`.
Completed runs preserve the streak for the next run. Loss or quit resets it.

`gp_name_encode` converts the N64 naming keyboard to the original six-character
full-width player-name encoding. It also generates the two random identity
salt bytes that the original stores at offsets14 and15. Call it once at player
creation and persist the full16-byte result. Re-encoding for every Card Pop
changes player identity. `gp_card_pop_pair` checks both peer histories and
applies both players' rewards atomically in memory.

Gift card transfer accepts1..60 free card IDs. It validates the whole transfer
before changing either player, removes all sent copies, and caps the receiver
at99 total copies. The original loses excess copies above that cap; the return
count lets the frontend show the effective number received before committing.
Recipe transfer copies a saved configuration, keeps the sender's recipe, and
does not create or remove physical card ownership. The transport must persist
**both** changed player states atomically before reporting success.

`gp_save_encode/decode` uses a6082-byte versioned little-endian payload with
CRC32 and semantic validation. Decode changes the state only on success and
uses a heap temporary to keep N64 stack usage small. The frontend owns storage
journaling and the larger save containing story state and player identity.

The story engine and this module must share the same three RNG bytes: sync
`GpState.rng` with packed `OwState.rng` across callbacks. The original has one
global random sequence; independently seeded streams change story outcomes.

```sh
make test
make differential
```

Unit tests cover all booster variants, starters, recipes, inventory limits,
deck validation, Card Pop pairs, gift rollback/capping, all mails, Challenge
Machine sequences/records, and save roundtrip/corruption/truncation/invalid
card rejection. The differential test uses the original SM83 routines as an
oracle:29 boosters x32 seeds compare every draw, final RNG, and collection;
256 Challenge Machine sequences and256 Card Pop selections also match.
The emulator appears only in the differential test executable.
