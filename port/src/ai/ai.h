/*
 * Declarations of every global routine and data table of the AI
 * (src/engine/duel/ai/, src/home/ai.asm, src/data/deck_ai_pointers.asm,
 * src/data/duel/ai_trainer_card_logic.asm), grouped by original file.
 * Read port/notes/AI_BRIEF.md first.
 *
 * Conventions (PORTING.md):
 *   - "carry" = true return value.
 *   - Register inputs are parameters, in register order (a, b, c, d, e, hl),
 *     named after what they hold; the comment names the register.
 *   - A value left in a register that some caller reads becomes the return value,
 *     or an out-parameter when the routine also returns carry. Out-parameters are
 *     never NULL and must be written on EVERY return path with the exact value the
 *     register holds at `ret` in the original (callers store some of them to RAM
 *     even on failure), unless the comment says otherwise ("unchanged if ...").
 *   - Inputs/outputs in WRAM/HRAM are noted in [brackets].
 *   - "(part X only)" = only called from the part that implements it.
 *
 * The AI is entered only through home/ai.asm (LoadOpponentDeck, AIDoAction_*,
 * declared in engine.h; implemented by part A in ai/home_ai.c). It calls back
 * into the duel core through AIMakeDecision (engine.h).
 *
 * Parts:  A = core.asm, init.asm, damage_calculation.asm, deck_ai.asm + decks/<deck>.asm,
 *             boss_deck_set_up.asm, home/ai.asm, data/deck_ai_pointers.asm,
 *             data/duel/ai_trainer_card_logic.asm
 *         B = trainer_cards.asm
 *         C = retreat.asm, hand_pokemon.asm, energy.asm, attacks.asm, special_attacks.asm
 *         D = pkmn_powers.asm, common.asm
 */
#ifndef TCG_AI_H
#define TCG_AI_H

#include "engine.h"
#include "effects/effects.h"
#include "gen/rom_blobs.h"
#include "ai_needs.h"

/* ================================================================ ROM lists reached through WRAM pointers
 * The six wAICardList* variables hold 16-bit Game Boy addresses of $00-terminated
 * lists in bank $05 ("AI Logic 1"), exactly as in the original (store_list_pointer).
 * Every such list is a blob in tools/rom_blobs.txt. Store with StoreListPointer(),
 * read back with AIListPtr(). Readers test the high byte for NULL exactly where
 * the original does (`ld a, [wAICardListX + 1] / or a / jr z`). */
#define BANK_AI_LOGIC_1 0x05

/* store_list_pointer wptr, label  ("AIActionTable_LegendaryMoltres.list_prize", ...) */
static inline void StoreListPointer(u8 *wptr, const char *label)
{
	u16 addr = RomAddr(label);
	if (!addr)
		tcg_fatal("StoreListPointer: label missing from tools/rom_blobs.txt");
	wr16(wptr, addr);
}

/* host pointer to the ROM bytes at bank $05 address addr (what `ld h, [ptr+1] / ld l, [ptr]` points to) */
static inline const u8 *AIListPtr(u16 addr)
{
	const u8 *p = RomPtr(BANK_AI_LOGIC_1, addr);
	if (!p)
		tcg_fatal("AIListPtr: address is not inside a ROM blob");
	return p;
}

/* ================================================================ data tables (part A)
 *
 * data/deck_ai_pointers.asm (ai/deck_ai_pointers.c) + decks/<deck>.asm (ai/deck_ai.c):
 * each AIActionTable_* has 6 entries indexed by AIACTION_* (entry 0 is the
 * "dw .do_turn ; unused" slot: fill it with the do_turn function). An action
 * function returns register a, which AIDoAction passes back (only the
 * forced_switch / ko_switch results are used: the Play Area location to switch to).
 * AIDoAction(a) == DeckAIPointerTable[wOpponentDeckID][a](). */
typedef u8 (*AIActionFunc)(void);
#define AI_ACTION_TABLE_SIZE (AIACTION_TAKE_PRIZE + 1)

extern const AIActionFunc AIActionTable_GeneralDecks[AI_ACTION_TABLE_SIZE];
extern const AIActionFunc AIActionTable_SamPractice[AI_ACTION_TABLE_SIZE];
extern const AIActionFunc AIActionTable_GeneralNoRetreat[AI_ACTION_TABLE_SIZE];
extern const AIActionFunc AIActionTable_LegendaryMoltres[AI_ACTION_TABLE_SIZE];
extern const AIActionFunc AIActionTable_LegendaryZapdos[AI_ACTION_TABLE_SIZE];
extern const AIActionFunc AIActionTable_LegendaryArticuno[AI_ACTION_TABLE_SIZE];
extern const AIActionFunc AIActionTable_LegendaryDragonite[AI_ACTION_TABLE_SIZE];
extern const AIActionFunc AIActionTable_FirstStrike[AI_ACTION_TABLE_SIZE];
extern const AIActionFunc AIActionTable_RockCrusher[AI_ACTION_TABLE_SIZE];
extern const AIActionFunc AIActionTable_GoGoRainDance[AI_ACTION_TABLE_SIZE];
extern const AIActionFunc AIActionTable_ZappingSelfdestruct[AI_ACTION_TABLE_SIZE];
extern const AIActionFunc AIActionTable_FlowerPower[AI_ACTION_TABLE_SIZE];
extern const AIActionFunc AIActionTable_StrangePsyshock[AI_ACTION_TABLE_SIZE];
extern const AIActionFunc AIActionTable_WondersOfScience[AI_ACTION_TABLE_SIZE];
extern const AIActionFunc AIActionTable_FireCharge[AI_ACTION_TABLE_SIZE];
extern const AIActionFunc AIActionTable_ImRonald[AI_ACTION_TABLE_SIZE];
extern const AIActionFunc AIActionTable_PowerfulRonald[AI_ACTION_TABLE_SIZE];
extern const AIActionFunc AIActionTable_InvincibleRonald[AI_ACTION_TABLE_SIZE];
extern const AIActionFunc AIActionTable_LegendaryRonald[AI_ACTION_TABLE_SIZE];
/* decks/unreferenced.asm (INCLUDEd at the top of core.asm, right after DeckAIPointerTable):
 * 7 words: [0] = dw $406c (not code: NULL), [1..6] = .do_turn, .do_turn, .star_duel,
 * .forced_switch, .ko_switch, .take_prize */
extern const AIActionFunc AIActionTable_Unreferenced[AI_ACTION_TABLE_SIZE + 1];

/* indexed by wOpponentDeckID. The extra last entry reproduces what the original reads
 * one past the end of the table (LoadOpponentDeck accepts wOpponentDeckID == NUM_DECK_IDS):
 * the word $406c = AIActionTable_Unreferenced + 2, i.e. &AIActionTable_Unreferenced[1]. */
extern const AIActionFunc *const DeckAIPointerTable[NUM_DECK_IDS + 1];

/* data/duel/ai_trainer_card_logic.asm (ai/ai_trainer_card_logic.c), used by _AIProcessHandTrainerCards.
 * decide: returns carry = play the card; *a = register a at return, which
 *         _AIProcessHandTrainerCards stores in wAITrainerCardParameter when carry
 *         (so it must be exact whenever the routine returns true).
 * play:   executes the card (return value unused). */
typedef bool (*AITrainerDecideFunc)(u8 *a);
typedef void (*AITrainerPlayFunc)(void);
struct AITrainerCardLogicEntry {
	u8 phase;                     /* AI_TRAINER_CARD_PHASE_*; $ff terminates the table */
	u8 card_id;
	AITrainerDecideFunc decide;
	AITrainerPlayFunc play;
};
#define NUM_AI_TRAINER_CARD_LOGIC 38
extern const struct AITrainerCardLogicEntry AITrainerCardLogic[NUM_AI_TRAINER_CARD_LOGIC + 1]; /* + { $ff } */

/* ================================================================ engine/duel/ai/core.asm (part A, ai/core.c) */
bool CheckIfAnyAttackKnocksOutDefendingCard(void);           /* in: [hTempPlayAreaLocation_ff9d] attacker; carry if an attack KOs the defending card; [wSelectedAttack] = that attack (EstimateDamage_VersusDefendingCard sets it) */
bool CheckIfAnyDefendingPokemonAttackDealsSameDamageAsHP(void); /* in: [hTempPlayAreaLocation_ff9d]; carry if a defending attack leaves it at exactly 0 HP; [wSelectedAttack] = that attack */
u8 FindHighestBenchScore(void);                              /* returns the bench location with the highest wPlayAreaAIScore (also [hTempPlayAreaLocation_ff9d]); carry always clear */
u8 AIEncourage(u8 a);                                        /* wAIScore += a, capped at 255; returns register a = the new wAIScore */
u8 AIDiscourage(u8 a);                                       /* wAIScore -= a, floored at 0; returns register a = 0 if wAIScore was 0, else (u8)(old wAIScore - a) (NOT clamped; retreat.asm .ai_score_bonus reads it) */
void LoadDefendingPokemonColorWRAndPrizeCards(void);         /* sets wAIPlayerColor/Weakness/Resistance/PrizeCount, wAIOpponentPrizeCount */
bool AITryUseAttack(void);                                   /* uses [wSelectedAttack] (OPPACTION_BEGIN_ATTACK/USE_ATTACK/ATTACK_ANIM_AND_DAMAGE); returns the carry of the last AIMakeDecision */
bool CheckIfEnergyIsUseful(u8 card);                         /* a = attached energy deck index; in: [wTempCardType], [wTempCardID]; carry if the energy is useful to that Pokemon */
u8 PickRandomBenchPokemon(void);                             /* returns a random bench location (one Random call) */
void AIPickPrizeCards(void);                                 /* takes [wNumberPrizeCardsToTake] random prizes into the hand */
void AIPlayInitialBasicCards(void);                          /* plays every Basic Pokemon of the hand to the play area */
bool CheckIfSelectedAttackIsUnusable(void);                  /* in: [hTempPlayAreaLocation_ff9d], [wSelectedAttack]; carry if unusable (gTextID may be set by the substatus checks); loads wLoadedAttack */
/* in: [hTempPlayAreaLocation_ff9d], [wSelectedAttack]; carry if no attack / Pkmn Power / not enough energy.
 * *b, *c, *e are written on every path (callers read b+c even without carry):
 *   no attack or Pkmn Power: b = c = e = 0 (carry);
 *   not enough:  b = basic energy still needed, c = colorless still needed, e = energy card ID
 *                of the missing basic type (ConvertColorToEnergyCardID(wTempLoadedAttackEnergyNeededType),
 *                i.e. FIRE_ENERGY when only colorless is missing) (carry);
 *   enough:      b = colorless cost, c = basic cost, e = LOW(ADDR_wLoadedAttackEnergyCost + 3)
 *                (leftover of the `ld de, wLoadedAttackEnergyCost` loop; LookForEnergyNeededInHand uses it) (no carry). */
bool CheckEnergyNeededForAttack(u8 *b, u8 *c, u8 *e);
bool CheckIfEnoughParticularAttachedEnergy(u8 a, u8 **hl, u8 *b); /* a = cost nibble, *hl = attached energy pointer, *b = type index; both advanced by 1; carry if not enough (sets wTempLoadedAttackEnergy*) (part A only) */
u8 ConvertColorToEnergyCardID(u8 type);                      /* a = energy type index (0-6) -> basic energy / DCE card ID */
bool Func_14323(void);                                       /* unreferenced: carry if wLoadedAttackEffectCommands has INITIAL_EFFECT_2 or REQUIRE_SELECTION (part A only) */
bool CheckIfCardCanBePlayed(u8 card);                        /* a = deck index (also -> [hTempCardIndex_ff9f]); carry if it can't be played now (gTextID may be set for trainers) */
bool CreateEnergyCardListFromHand(void);                     /* energy cards of the hand -> wDuelTempList; carry if none (register a = wDuelTempList[0], i.e. $ff when carry); preserves bc/de/hl */
bool LookForCardIDInHand(u8 card_id, u8 *a);                 /* carry if NOT found; *a = deck index if found, else the card ID of the last hand card checked (0 if the hand is empty); energy.asm stores it in hTemp_ffa0 either way */
void AIProcessHandTrainerCards(u8 phase);                    /* a = AI_TRAINER_CARD_PHASE_*; farcall _AIProcessHandTrainerCards */
bool LookForCardIDInHandList_Bank5(u8 card_id, u8 *a);       /* builds the hand list in wDuelTempList; carry if found; *a = deck index, or $ff if not found (Sam's script stores it anyway) */
bool LookForCardIDInPlayArea_Bank5(u8 card_id, u8 b, u8 *a); /* b = first PLAY_AREA_* to check; carry if found; *a = location if found, else $ff (empty slot reached) or MAX_PLAY_AREA_POKEMON (all slots checked); [wTempCardIDToLook], wLoadedCard1 */
bool AIAttachEnergyInHandToCardInPlayArea(u8 d, u8 e);       /* d = Pokemon card ID, e = energy card ID; plays that energy from hand to it (OPPACTION_PLAY_ENERGY); returns the carry of AIMakeDecision (no carry if not in hand) */
bool AIAttachEnergyInHandToCardInBench(u8 d, u8 e);          /* same, only looking in the bench */
bool CheckEnergyNeededForAttackAfterDiscard(u8 *b, u8 *c, u8 *e); /* like CheckEnergyNeededForAttack after removing the energy AIPickEnergyCardToDiscard picks (same outputs) */
void ClearMemory_Bank5(u8 a, u8 *hl);                        /* a = byte count, hl = start (part A only) */
u8 ConvertHPToDamageCounters_Bank5(u8 hp);                   /* floor(a / 10) */
u8 CalculateBDividedByA_Bank5(u8 a, u8 b);                   /* floor(b / a) (a = divisor, b = dividend) */
u8 CountNumberOfEnergyCardsAttached(u8 e);                   /* e = PLAY_AREA_*; number of energy cards (DCE counts once); calls GetPlayAreaCardAttachedEnergies */
bool LookForCardIDInLocation_Bank5(u8 a, u8 e, u8 *idx);     /* a = CARD_LOCATION_*, e = card ID; carry if found; *idx = deck index (registers a and e), DECK_SIZE if not found (core.asm stores it in hTemp_ffa0 either way) */
u8 CountOppEnergyCardsInHandAndAttached(void);               /* energy cards in hand + attached in the turn duelist's play area (also [wTempAI]) */
bool RemoveCardIDInList(u8 e, u8 *hl, u8 *a);                /* e = card ID, hl = $ff-terminated deck index list (unchanged pointer); carry if found and removed; *a = its deck index (also [hTempCardIndex_ff98]) */
bool TrySetUpBossStartingPlayArea(void);                     /* plays the Arena/Bench priority lists (wAICardListArenaPriority/BenchPriority, via AIListPtr); carry if no Arena card could be played */
bool Func_1585b(const u8 *hl, u8 *a);                        /* unreferenced; hl = $00-terminated 3-byte entries; carry if a listed bench card has enough energy, *a = its location (part A only) */
bool Func_15886(const u8 *hl);                               /* unreferenced; tries AITryToPlayEnergyCard on listed cards lacking energy (part A only) */
void CopyListWithFFTerminatorFromHLToDE_Bank5(const u8 *hl, u8 *de); /* copies through the $ff terminator */
bool CheckDamageToMrMime(u8 loc);                            /* a = location; carry if the defending card is not Mr Mime, or if loc can damage it */
bool CheckIfActiveCardCanKnockOut(void);                     /* carry if the arena card can KO the defending card with a usable attack; sets [hTempPlayAreaLocation_ff9d] = 0 */
bool CanArenaCardUseNonResidualAttack(void);                 /* carry if the arena card has a usable non-residual attack; sets [hTempPlayAreaLocation_ff9d] = 0, [wSelectedAttack] */
bool LookForEnergyNeededInHand(void);                        /* in: [hTempPlayAreaLocation_ff9d]; checks both attacks; carry if the needed energy is in hand */
bool LookForEnergyNeededForAttackInHand(void);               /* in: [hTempPlayAreaLocation_ff9d], [wSelectedAttack]; carry if the needed energy is in hand */
void SortTempHandByIDList(void);                             /* sorts wDuelTempList by the list at wAICardListPlayFromHandPriority (via AIListPtr) */
bool CheckEnergyFlagsNeededInList(u8 flags);                 /* a = energy flags (*_F bits); carry if wDuelTempList has a matching energy card */
u8 GetAttacksEnergyCostBits(u8 card);                        /* a = deck index (loads wLoadedCard2); energy flag bits of both attacks ($ff if any colorless) */
bool CheckForEvolutionInList(u8 card, u8 *evolution);        /* a = deck index; carry if a card of wDuelTempList evolves it; *evolution = that deck index (only on carry) */
bool CheckForEvolutionInDeck(u8 card, u8 *evolution);        /* a = deck index; carry if a card in the deck evolves it; *evolution = that deck index (only on carry) */
bool LookForCardThatIsKnockedOutOnDevolution(u8 *a);         /* carry if a non-turn duelist's evolved card would be KO'd by devolving; *a = its location; without carry *a = the [hTempPlayAreaLocation_ff9d] value on entry (AISelectSpecialAttackParameters stores it anyway) */
bool CheckIfArenaCardIsFullyPowered(void);                   /* carry if arena HP > max/2, can't evolve further, and can use its second attack */
u8 CountNumberOfSetUpBenchPokemon(void);                     /* number of "set up" bench Pokemon (nonzero == carry); restores [wSelectedAttack], [hTempPlayAreaLocation_ff9d] */
bool AISelectSpecialAttackParameters(void);                  /* for Devolution Beam / Energy Absorption / Teleport / Energy Spike: sets hTemp_ffa0 / hTempPlayAreaLocation_ffa1 / hTempRetreatCostCards; carry if a selection was made; restores [wSelectedAttack] */
/* in: [hTempPlayAreaLocation_ff9d], [wSelectedAttack]. carry if not an attack, not enough energy, or exactly
 * enough; *a is written on every path (trainer_cards.asm reads it regardless of carry):
 * no attack: 0 (no name) or POKEMON_POWER (the category tested); otherwise
 * (u8)(wTotalAttachedEnergies - wTempLoadedAttackEnergyCost - colorless cost) (0 when exact, wrapped when not enough). */
bool CheckIfNoSurplusEnergyForAttack(u8 *a);
void CalculateParticularAttachedEnergyNeeded(u8 a, u8 **hl, u8 *b); /* like CheckIfEnoughParticularAttachedEnergy for CheckIfNoSurplusEnergyForAttack (flags unused) (part A only) */
bool CheckCardEvolutionInHandOrDeck(u8 card, u8 *evolution); /* a = deck index; carry if a card in hand or deck evolves it; *evolution = that deck index (only on carry) */
bool CheckIfCanDamageDefendingPokemon(u8 loc);               /* a = location; carry if any usable attack deals damage; sets [hTempPlayAreaLocation_ff9d], [wSelectedAttack] */
bool CheckIfDefendingPokemonCanKnockOut(u8 *damage);         /* in: [hTempPlayAreaLocation_ff9d]; carry if a defending attack KOs it; *damage = largest KO damage (only meaningful on carry; 0 otherwise); sets wAIFirstAttackDamage/wAISecondAttackDamage */
bool CheckIfOpponentHasBossDeckID(void);                     /* carry if LEGENDARY_MOLTRES_DECK_ID <= [wOpponentDeckID] < MUSCLES_FOR_BRAINS_DECK_ID (preserves a) */
bool CheckIfNotABossDeckID(void);                            /* carry if not a boss deck and sReceivedLegendaryCards == 0 (GetSRAMReceivedLegendaryCards) */
bool AIChooseRandomlyNotToDoAction(void);                    /* carry with 50% (decks listed in the asm) / 25% (others) chance: one Random(4) call, only if CheckIfNotABossDeckID returns carry (else no carry, no Random); preserves de/hl */
bool CheckForBenchIDAtHalfHPAndCanUseSecondAttack(u8 card_id); /* a = card ID; carry if such a bench card has HP > max/2 and can use its second attack; restores [wSelectedAttack], [hTempPlayAreaLocation_ff9d] */
void RaiseAIScoreToAllMatchingIDsInBench(u8 card_id);        /* a = card ID; wPlayAreaEnergyAIScore[loc] += 5 for each bench match */
void HandleAIEnergyScoringForRepeatedBenchPokemon(void);     /* adjusts wPlayAreaEnergyAIScore for repeated bench Pokemon */
void HandleLegendaryArticunoEnergyScoring(void);             /* ScoreLegendaryArticunoCards if [wOpponentDeckID] == LEGENDARY_ARTICUNO_DECK_ID */

/* ================================================================ engine/duel/ai/damage_calculation.asm (part A, ai/damage_calculation.c) */
void EstimateDamage_VersusDefendingCard(u8 attack);          /* a = attack index -> [wSelectedAttack]; in: [hTempPlayAreaLocation_ff9d]; out: wDamage, wAIMinDamage, wAIMaxDamage */
void CalculateDamage_VersusDefendingPokemon(void);           /* applies W/R, PlusPower/Defender, poison to wAIMinDamage/wAIMaxDamage/wDamage (part A only) */
void EstimateDamage_FromDefendingPokemon(u8 attack);         /* a = defending attack index -> [wSelectedAttack]; in: [hTempPlayAreaLocation_ff9d] receiver; out: wDamage, wAIMinDamage, wAIMaxDamage */
void CalculateDamage_FromDefendingPokemon(void);             /* (part A only) */

/* ================================================================ engine/duel/ai/init.asm (part A, ai/init.c) */
void InitAIDuelVars(void);                                   /* clears wAIDuelVars (incl. the wAICardList* pointers); wAIPokedexCounter = 5, wAIPeekedPrizes = $ff (part A only) */
void InitAITurnVars(void);                                   /* per-turn AI vars and the wAIBarrierFlagCounter (Mewtwo mill) logic (part A only) */

/* ================================================================ engine/duel/ai/deck_ai.asm + decks/<deck>.asm (part A, ai/deck_ai.c)
 * The .do_turn/.start_duel/... locals become static functions of deck_ai.c (AIActionFunc).
 * Turn routines return carry like the original `ret c` (unused by AIDoAction). (part A only) */
bool AIMainTurnLogic(void);                                  /* decks/general.asm */
void AIProcessRetreat(void);                                 /* decks/general.asm */
bool IsAIPracticeScriptedTurn(void);                         /* decks/sams_practice.asm: carry if [wDuelTurns] / 2 >= 7 */
void SetSamsStartingPlayArea(void);
u8 GetPlayAreaLocationOfRaticateOrRattata(void);             /* returns the location (also [hTempPlayAreaLocation_ff9d]) */
bool AIPerformScriptedTurn(void);
bool AIDoTurn_GeneralNoRetreat(void);                        /* decks/general_no_retreat.asm */
bool AIDoTurn_LegendaryMoltres(void);
bool AIDoTurn_LegendaryZapdos(void);
void ScoreLegendaryArticunoCards(void);                      /* decks/legendary_articuno.asm (called by HandleLegendaryArticunoEnergyScoring) */
bool AIDoTurn_LegendaryArticuno(void);
bool AIDoTurn_LegendaryDragonite(void);
bool AIDoTurn_LegendaryRonald(void);

/* ================================================================ engine/duel/ai/boss_deck_set_up.asm (part A, ai/boss_deck_set_up.c) */
void SetUpBossStartingHandAndDeck(void);                     /* reshuffles until the hand/deck meet the boss minimums (reads wAICardListAvoidPrize via AIListPtr) (part A only) */

/* ================================================================ home/ai.asm (part A, ai/home_ai.c)
 * LoadOpponentDeck, AIDoAction_Turn/StartDuel/ForcedSwitch/KOSwitch/TakePrize, AIDoAction:
 * declared in engine.h (keep those signatures). */

/* ================================================================ engine/duel/ai/trainer_cards.asm (part B, ai/trainer_cards.c)
 * Every AIDecide_* has the AITrainerDecideFunc signature (see AITrainerCardLogic);
 * every AIPlay_* is an AITrainerPlayFunc. Input: [wAITrainerCardToPlay],
 * [wAITrainerCardParameter] (AIPlay_*). */
void _AIProcessHandTrainerCards(u8 phase);                   /* a = AI_TRAINER_CARD_PHASE_*; [wAITrainerCardPhase], wPreviousAIFlags/wCurrentAIFlags */
void AIPlay_Potion(void);
bool AIDecide_Potion_Phase07(u8 *a);
bool AIDecide_Potion_Phase10(u8 *a);
void AIPlay_SuperPotion(void);
bool AIDecide_SuperPotion_Phase08(u8 *a);
bool AIDecide_SuperPotion_Phase11(u8 *a);
void AIPlay_Defender(void);
bool AIDecide_Defender_Phase13(u8 *a);
bool AIDecide_Defender_Phase14(u8 *a);
void AIPlay_PlusPower(void);
bool AIDecide_PlusPower_Phase13(u8 *a);
bool AIDecide_PlusPower_Phase14(u8 *a);
void AIPlay_Switch(void);
bool AIDecide_Switch(u8 *a);
void AIPlay_GustOfWind(void);
bool AIDecide_GustOfWind(u8 *a);
void AIPlay_Bill(void);
bool AIDecide_Bill(u8 *a);
void AIPlay_EnergyRemoval(void);
bool AIDecide_EnergyRemoval(u8 *a);
void AIPlay_SuperEnergyRemoval(void);
bool AIDecide_SuperEnergyRemoval(u8 *a);
void AIPlay_PokemonBreeder(void);
bool AIDecide_PokemonBreeder(u8 *a);
void AIPlay_ProfessorOak(void);
bool AIDecide_ProfessorOak(u8 *a);
void AIPlay_EnergyRetrieval(void);
bool AIDecide_EnergyRetrieval(u8 *a);
u8 *RemoveCardFromList(u8 *hl);                              /* hl = element after the one to remove; shifts the $ff-terminated list down; returns hl - 1 (register hl after the call) (part B only) */
bool FindDuplicateCards(u8 *hl, u8 *a);                      /* hl = $ff-terminated deck index list; carry if NO duplicates; else *a = second card of a Pokemon pair (wce0f[0]) or, if none, of a non-Pokemon pair (wce0f[1]) (part B only) */
void AIPlay_SuperEnergyRetrieval(void);
bool AIDecide_SuperEnergyRetrieval(u8 *a);
void FindAndRemoveCardFromList(u8 a, u8 *hl);                /* a = deck index that must be in list hl; removes it (hl preserved) (part B only) */
void AIPlay_PokemonCenter(void);
bool AIDecide_PokemonCenter(u8 *a);
void AIPlay_ImposterProfessorOak(void);
bool AIDecide_ImposterProfessorOak(u8 *a);
void AIPlay_EnergySearch(void);
bool AIDecide_EnergySearch(u8 *a);
void AIPlay_Pokedex(void);
bool AIDecide_Pokedex(u8 *a);
bool PickPokedexCards_Unreferenced(u8 *a);                   /* unreferenced; falls through into PickPokedexCards (part B only) */
bool PickPokedexCards(u8 *a);                                /* tail of AIDecide_Pokedex (same contract); order -> wce1a.. (part B only) */
void AIPlay_FullHeal(void);
bool AIDecide_FullHeal(u8 *a);
void AIPlay_MrFuji(void);
bool AIDecide_MrFuji(u8 *a);
void AIPlay_ScoopUp(void);
bool AIDecide_ScoopUp(u8 *a);                                /* also called by AIDecide_FullHeal (carry only) */
void AIPlay_Maintenance(void);
bool AIDecide_Maintenance(u8 *a);
void AIPlay_Recycle(void);
bool AIDecide_Recycle(u8 *a);
void AIPlay_Lass(void);
bool AIDecide_Lass(u8 *a);
void AIPlay_ItemFinder(void);
bool AIDecide_ItemFinder(u8 *a);
void AIPlay_Imakuni(void);
bool AIDecide_Imakuni(u8 *a);
void AIPlay_Gambler(void);                                   /* keeps the RNG cheat: wRNG1/wRNG2/wRNGCounter = $50 around the effect, then restored (not for IMAKUNI_DECK_ID) */
bool AIDecide_Gambler(u8 *a);
void AIPlay_Revive(void);
bool AIDecide_Revive(u8 *a);
void AIPlay_PokemonFlute(void);
bool AIDecide_PokemonFlute(u8 *a);
void AIPlay_ClefairyDollOrMysteriousFossil(void);
bool AIDecide_ClefairyDollOrMysteriousFossil(u8 *a);
void AIPlay_Pokeball(void);
bool AIDecide_Pokeball(u8 *a);
void AIPlay_ComputerSearch(void);
bool AIDecide_ComputerSearch(u8 *a);
bool AIDecide_ComputerSearch_RockCrusher(u8 *a);             /* tail-jumped from AIDecide_ComputerSearch (same contract) (part B only) */
bool AIDecide_ComputerSearch_WondersOfScience(u8 *a);        /* (part B only) */
bool AIDecide_ComputerSearch_FireCharge(u8 *a);              /* (part B only) */
bool AIDecide_ComputerSearch_Anger(u8 *a);                   /* (part B only) */
void AIPlay_PokemonTrader(void);
bool AIDecide_PokemonTrader(u8 *a);
bool AIDecide_PokemonTrader_LegendaryMoltres(u8 *a);         /* tail-jumped from AIDecide_PokemonTrader (same contract) (part B only) */
bool AIDecide_PokemonTrader_LegendaryArticuno(u8 *a);        /* (part B only) */
bool AIDecide_PokemonTrader_LegendaryDragonite(u8 *a);       /* (part B only) */
bool AIDecide_PokemonTrader_LegendaryRonald(u8 *a);          /* (part B only) */
bool AIDecide_PokemonTrader_BlisteringPokemon(u8 *a);        /* (part B only) */
bool AIDecide_PokemonTrader_SoundOfTheWaves(u8 *a);          /* (part B only) */
bool AIDecide_PokemonTrader_PowerGenerator(u8 *a);           /* (part B only) */
bool AIDecide_PokemonTrader_FlowerGarden(u8 *a);             /* (part B only) */
bool AIDecide_PokemonTrader_StrangePower(u8 *a);             /* (part B only) */
bool AIDecide_PokemonTrader_Flamethrower(u8 *a);             /* (part B only) */

/* ================================================================ engine/duel/ai/retreat.asm (part C, ai/retreat.c) */
bool AIDecideWhetherToRetreat(void);                         /* carry if AI decides to retreat (wAIScore >= 131, or the Fossil/Doll rule); sets wAIPlayEnergyCardForRetreat */
void SetAIRetreatFlags(void);                                /* wAIRetreatFlags (part C only) */
bool AIDecideBenchPokemonToSwitchTo(u8 *a);                  /* carry if fewer than 2 Pokemon in play: *a = that count (0/1); else no carry, *a = best bench location (also [hTempPlayAreaLocation_ff9d]); reads wAICardListRetreatBonus via AIListPtr. *a is what AIDoAction_ForcedSwitch/KOSwitch store */
bool AITryToRetreat(u8 loc);                                 /* a = bench location to retreat to; carry if it couldn't retreat (Asleep/Paralyzed, cost not payable, Fossil/Doll without bench) */

/* ================================================================ engine/duel/ai/hand_pokemon.asm (part C, ai/hand_pokemon.c) */
bool AIDecidePlayPokemonCard(void);                          /* plays Basic Pokemon then jumps to AIDecideEvolution; carry if the turn ended */
bool AIDecideEvolution(void);                                /* always returns no carry (part C only) */
void AIDecideSpecialEvolutions(void);                        /* in: [wOpponentDeckID], [wLoadedCard2ID], [hTempPlayAreaLocation_ff9d]; adjusts wAIScore (part C only) */
void AIDecidePlayLegendaryBirds(void);                       /* in: [wLoadedCard1ID]; adjusts wAIScore (part C only) */

/* ================================================================ engine/duel/ai/energy.asm (part C, ai/energy.c) */
bool Func_16488(void);                                       /* unreferenced; continues at AIProcessAndTryToPlayEnergy.has_logic_flags (part C only) */
bool AIProcessButDontPlayEnergy_SkipEvolution(void);         /* carry if an energy would be played; location in [hTempPlayAreaLocation_ff9d]; wPlayAreaAIScore/wAIScore restored */
bool AIProcessButDontPlayEnergy_SkipEvolutionAndArena(void); /* same, bench only */
void RetrievePlayAreaAIScoreFromBackup1(void);               /* wTempPlayAreaAIScore -> wPlayAreaAIScore, wTempAIScore -> wAIScore; preserves af (callers `scf / jp` it: return their own carry) (part C only) */
bool AIProcessAndTryToPlayEnergy(void);                      /* carry if an energy card was played */
bool AIProcessEnergyCards(void);                             /* (part C only) */
void DetermineAIScoreOfAttackEnergyRequirement(u8 attack);   /* a = attack index -> [wSelectedAttack]; in: [wTempAI] (evolution in hand or $ff) (part C only) */
bool FindPlayAreaCardWithHighestAIScore(void);               /* carry if found; location in [hTempPlayAreaLocation_ff9d] (part C only) */
bool CheckIfEvolutionNeedsEnergyForAttack(u8 *b, u8 *c, u8 *e); /* in: [hTempPlayAreaLocation_ff9d], [wSelectedAttack]; carry if an evolution in hand/deck lacks energy for it; on carry *b, *c, *e = the CheckEnergyNeededForAttack outputs (AITryToPlayEnergyCard goes on to use them) (part C only) */
bool GetEnergyCardForDiscardOrEnergyBoostAttack(u8 *b, u8 *c, u8 *e); /* in: [hTempPlayAreaLocation_ff9d], [wSelectedAttack] (loads wLoadedCard2); carry unless ZapdosLv64's Thunderbolt: *b = TRUE, *c = FALSE, *e = energy card ID; or *b = FALSE, *c = TRUE, *e unchanged (Fire Spin, Big Eggsplosion); no carry: *b = the card ID, *c and *e unchanged (part C only) */
bool AITryToPlayEnergyCard(void);                            /* in: [hTempPlayAreaLocation_ff9d], energy list in wDuelTempList; carry if an energy card was played */
bool CheckSpecificDecksToAttachDoubleColorless(void);        /* carry if a DCE should be attached; its deck index in [hTemp_ffa0] (part C only) */

/* ================================================================ engine/duel/ai/attacks.asm (part C, ai/attacks.c) */
bool AIProcessButDontUseAttack(void);                        /* carry if an attack would be used ([wSelectedAttack]); scores restored */
void RetrievePlayAreaAIScoreFromBackup2(void);               /* identical to RetrievePlayAreaAIScoreFromBackup1; preserves af (part C only) */
bool AIProcessAndTryToUseAttack(void);                       /* carry if an attack was chosen and attempted */
bool AIProcessAttacks(void);                                 /* in: [wAIExecuteProcessedAttack] (part C only) */
void GetAIScoreOfAttack(u8 attack);                          /* a = attack index -> [wSelectedAttack]; result in wAIScore (part C only) */

/* ================================================================ engine/duel/ai/special_attacks.asm (part C, ai/special_attacks.c) */
u8 HandleSpecialAIAttacks(void);                             /* in: [hTempPlayAreaLocation_ff9d], [wSelectedAttack]; returns score ($80 = neutral, 0 = dismiss) (part C only) */
void CheckWhetherToSwitchToFirstAttack(void);                /* may set [wSelectedAttack] = FIRST_ATTACK_OR_PKMN_POWER (part C only) */
bool CheckIfAnyBasicPokemonInDeck(void);                     /* carry if the deck has a Basic Pokemon (part C only) */

/* ================================================================ engine/duel/ai/pkmn_powers.asm (part D, ai/pkmn_powers.c) */
void HandleAIEnergyTrans(u8 mode);                           /* a = AI_ENERGY_TRANS_* -> [wce06] */
void AIEnergyTransTransferEnergyToBench(void);               /* tail of HandleAIEnergyTrans (part D only) */
bool HandleAIPkmnPowers(void);                               /* Heal/Shift/Peek/Strange Behavior/Curse; carry if the turn ended */
void HandleAIHeal(u8 c);                                     /* c = location of Vileplume; in: [wce08] = its deck index (part D only) */
void HandleAIShift(u8 c);                                    /* c = location of Venomoth (part D only) */
void HandleAIPeek(u8 c);                                     /* c = location of Mankey (part D only) */
void HandleAIStrangeBehavior(u8 c);                          /* c = location of Slowbro (part D only) */
bool HandleAICurse(u8 c);                                    /* c = location of Gengar; carry = carry of the last AIMakeDecision (turn ended), no carry if not used (part D only) */
void HandleAICowardice(void);
void HandleAIDamageSwap(void);
void HandleAIGoGoRainDanceEnergy(void);

/* ================================================================ engine/duel/ai/common.asm (part D, ai/common.c) */
bool CheckIfPlayerHasPokemonOtherThanMewtwoLv53(void);       /* carry if the player's deck has a Pokemon other than MewtwoLv53 */
bool HandleAIAntiMewtwoDeckStrategy(void);                   /* carry = run the normal turn; no carry = skip to the attack (after processing phase 5 Trainers) */
bool FindBasicEnergyCardsInLocation(u8 loc, u8 *count);      /* a = CARD_LOCATION_* -> [wTempAI]; basic energy -> wDuelTempList ($ff-terminated only if found); carry if none; *count = number found (0 on carry) */
u8 AIPickEnergyCardToDiscard(u8 loc);                        /* a = location (-> [hTempPlayAreaLocation_ff9d]); deck index of the attached energy to discard, $ff if none */
u8 PickAttachedEnergyCardToRemove(u8 loc);                   /* a = location; deck index of the attached energy to remove, $ff if none */
u8 PickTwoAttachedEnergyCards(u8 loc, u8 *b);                /* a = location; returns a = [wTempAI] (first card) and *b = [wCurCardCanAttack] (second card); fewer than 2 energies: returns $ff, *b unchanged */
void CopyListWithFFTerminatorFromHLToDE_Bank8(const u8 *hl, u8 *de);
void ClearMemory_Bank8(u8 a, u8 *hl);                        /* a = byte count, hl = start */
u8 CountOppEnergyCardsInHand(void);                          /* returns register a: number of energy cards in hand; $ff (with carry) if none - callers use the value either way */
u8 ConvertHPToDamageCounters_Bank8(u8 hp);                   /* floor(a / 10) */
u16 CalculateWordTensDigit(u16 hl);                          /* floor(hl / 10) */
u8 CalculateBDividedByA_Bank8(u8 a, u8 b);                   /* floor(b / a) (a = divisor, b = dividend) */
bool LookForCardIDInLocation_Bank8(u8 a, u8 e, u8 *idx);     /* a = CARD_LOCATION_*, e = card ID; carry if found; *idx = deck index (DECK_SIZE if not found) */
bool LookForCardIDInHandList_Bank8(u8 card_id, u8 *a);       /* builds the hand list in wDuelTempList; carry if found; *a = deck index ($ff if not found) */
bool LookForCardIDInDeck_GivenCardIDInHandAndPlayArea(u8 a, u8 b, u8 *idx); /* a = card ID 1, b = card ID 2; carry if ID 1 is in deck, ID 2 in hand/play area and ID 1 not in hand/play area; *idx = register a (deck index of ID 1 on carry) */
bool LookForCardIDInHandAndPlayArea(u8 card_id);             /* a = card ID; carry if found in hand (LookForCardIDInHandList_Bank8: rebuilds wDuelTempList) or play area (LookForCardIDInPlayArea_Bank8 from the arena); callers only test carry */
bool LookForCardIDInDeck_GivenCardIDInHand(u8 a, u8 b, u8 *idx); /* like the above but ID 2 only looked for in hand */
bool LookForCardIDInPlayArea_Bank8(u8 card_id, u8 b, u8 *a); /* like LookForCardIDInPlayArea_Bank5 */
bool RemoveFromListDifferentCardOfGivenType(u8 d, u8 e, u8 *hl, u8 *a); /* d = type allowed (0 trainer, 1 Pokemon, 2 energy), e = deck index to avoid, hl = list (shuffled first: ShuffleCards(hl, CountCardsInDuelTempList())); carry if removed; *a = removed deck index (also [hTempCardIndex_ff98]) */
bool LookForCardIDToTradeWithDifferentHandCard(u8 a, u8 e, u8 *deck_card, u8 *hand_card); /* a = card ID 1, e = card ID 2; carry if ID 1 is in deck (not in hand) and the hand has a Pokemon other than ID 2: *deck_card (a) = deck index of ID 1, *hand_card (e) = that hand card */
bool CheckIfHasCardIDInHand(u8 card_id, u8 *a);              /* carry if the hand has (at least) two cards of card_id; *a = deck index of the second ($ff if not) */
u8 CountPokemonCardsInHandAndInPlayArea(void);               /* also [wTempAI] */
bool FindDuplicatePokemonCards(u8 *a);                       /* carry if the hand has two Pokemon with the same ID; *a = deck index of one ([wTempAI]) */
bool AICheckIfAttackIsHighRecoil(void);                      /* carry if AIProcessButDontUseAttack chooses an attack that is NOT high recoil */

#endif
