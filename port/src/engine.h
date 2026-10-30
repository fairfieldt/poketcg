/*
 * Declarations of every ported engine routine, grouped by original file.
 * See PORTING.md for the conventions (names, carry flag -> bool, gTextID ...).
 */
#ifndef TCG_ENGINE_H
#define TCG_ENGINE_H

#include <string.h>
#include "common.h"
#include "gen/text.h"
#include "gen/effect_commands.h"
#include "duel_ui.h"

/*
 * gTextID models register hl when an asm routine returns a text ID in it to
 * its caller (typically `ldtx hl, SomeText` + `scf` + `ret`), e.g. effect
 * functions returning carry from EFFECTCMDTYPE_INITIAL_EFFECT_*, or
 * CheckIsIncapableOfUsingPkmnPower. Set it exactly where the original loads hl.
 */
extern u16 gTextID;

/* ---------------------------------------------------------------- home/duel.asm (home_duel.c) */
void SaveDuelStateToSRAM(void);
bool CopyDeckData(const u8 *deck);
u8 CountPrizes(void);
void ShuffleDeck(void);
bool DrawCardFromDeck(u8 *card);                 /* carry if deck empty; *card = deck index (may be NULL) */
u8 ReturnCardToDeck(u8 card);
void SearchCardInDeckAndAddToHand(u8 card);
void AddCardToHand(u8 card);
void RemoveCardFromHand(u8 card);
void MoveHandCardToDiscardPile(u8 card);
void PutCardInDiscardPile(u8 card);
u8 MoveDiscardPileCardToHand(u8 card);
extern const u8 PowersOf2[8];
u8 CheckPrizeTaken(u8 prize);                    /* 0 (z) if taken */
bool CreateDiscardPileCardList(void);
bool CreateDeckCardList(u8 *count);              /* count may be NULL */
bool CreateArenaOrBenchEnergyCardList(u8 loc);
bool CreateHandCardList(u8 *count);              /* count (number of cards in hand) may be NULL */
void SortHandCardsByID(void);
void ShuffleCards(u8 *list, u8 n);
void SortCardsInDuelTempListByID(void);
void SortCardsInListByID(void);                  /* list pointer (GB address) in hTempListPtr_ff99 */
void SortCardsInListByID_CheckForListTerminator(void);
u16 GetCardIDFromDeckIndex_bc(u8 card);
u8 GetCardInDuelTempList_OnlyDeckIndex(u8 i);
u8 GetCardInDuelTempList(u8 i, u8 *card_id);     /* returns deck index; card_id may be NULL */
u8 GetCardIDFromDeckIndex(u8 card);
bool RemoveCardFromDuelTempList(u8 card);        /* carry if the list becomes empty */
u8 CountCardsInDuelTempList(void);
u8 _GetCardIDFromDeckIndex(u8 card);
u8 LoadCardDataToBuffer1_FromDeckIndex(u8 card); /* returns card ID (registers a and e) */
u8 LoadCardDataToBuffer2_FromDeckIndex(u8 card);
bool EvolvePokemonCardIfPossible(void);
void EvolvePokemonCard(void);
bool CheckIfCanEvolveInto(u8 d, u8 e, u8 *a);    /* d = deck index of evolution, e = play area loc; a may be NULL */
bool CheckIfCanEvolveInto_BasicToStage2(u8 d, u8 e);
void ClearAllStatusConditions(void);
bool PutHandPokemonCardInPlayArea(u8 card, u8 *loc); /* carry if full; loc may be NULL */
u8 PutHandCardInPlayArea(u8 card, u8 e);
void MovePlayAreaCardToDiscardPile(u8 e);
void EmptyPlayAreaSlot(u8 e);
void ShiftAllPokemonToFirstPlayAreaSlots(void);
void ShiftTurnPokemonToFirstPlayAreaSlots(void);
void SwapArenaWithBenchPokemon(u8 e);
void SwapPlayAreaPokemon(u8 d, u8 e);
void GetPlayAreaCardAttachedEnergies(u8 e);
u8 CountCardIDInLocation(u8 turn, u8 b, u8 e);   /* turn = register h ($c2/$c3), b = location, e = card id */
u8 GetTurnDuelistVariable(u8 off);
u8 GetNonTurnDuelistVariable(u8 off);
void ProcessPlayedPokemonCard(void);
void CopyAttackDataAndDamage_FromCardID(u8 card_id, u8 d, u8 e);
void CopyAttackDataAndDamage_FromDeckIndex(u8 d, u8 e);
void CopyAttackDataAndDamage(u8 e);
void UpdateArenaCardIDsAndClearTwoTurnDuelVars(void);
bool UseAttackOrPokemonPower(void);
bool PlayAttackAnimation_DealAttackDamage(void);
bool HandleAfterDamageEffects(void);
bool DisplayUsePokemonPowerScreen_WaitForInput(u16 text);
bool DrawWideTextBox_WaitForInput_ReturnCarry(u16 text);
bool ClearNonTurnTemporaryDuelvars_ResetCarry(void);
bool HandleConfusionDamageToSelf(void);
bool UsePokemonPower(void);
void SendAttackDataToLinkOpponent(void);
u16 ApplyTransparencyIfApplicable(u16 de);
bool CheckSelfConfusionDamage(void);
bool PlayTrainerCard(void);
void LoadNonPokemonCardEffectCommands(void);
void DealRecoilDamageToSelf(u8 a);
void DealConfusionDamageToSelf(u8 a);
u16 ApplyDamageModifiers_DamageToTarget(void);
extern const u8 InvertedPowersOf2[8];
u8 TranslateColorToWR(u8 color);
u16 ApplyDamageModifiers_DamageToSelf(void);
u16 ApplyAttachedPlusPower(u8 b, u16 de);
u16 ApplyAttachedDefender(u8 b, u16 de);
bool SubtractHP(u8 *hl, u16 de);                 /* carry if HP is not 0 afterwards */
bool PrintPlayAreaCardKnockedOutIfNoHP(u8 e);
bool PrintKnockedOutIfHLZero(const u8 *hl);
bool PrintKnockedOut(void);
void DealDamageToPlayAreaPokemon_RegularAnim(u8 b, u16 de);
void DealDamageToPlayAreaPokemon(u8 b, u16 de);
void DrawDuelMainScene_PrintPokemonsAttackText(void);
void PrintPokemonsAttackText(void);
void Func_1bb4(void);
bool PrintFailedEffectText(void);
u8 GetPlayAreaCardRetreatCost(void);
void MoveCardToDiscardPileIfInPlayArea(u8 turn, u8 card_id);
u8 GetCardDamageAndMaxHP(u8 e, u8 *max_hp);      /* returns damage; max_hp (register c) may be NULL */
bool CheckLoadedAttackFlag(u8 a);
void SwapTurn(void);

/* ---------------------------------------------------------------- home/substatus.asm (substatus.c) */
u16 HandleDoubleDamageSubstatus(u16 de);
u16 HandleDamageReduction(u16 de);
u16 HandleDamageReductionExceptSubstatus2(u16 de);
u16 HandleDamageReductionOrNoDamageFromPkmnPowerEffects(u16 de);
void HandleStrikesBack_AgainstDamagingAttack(u16 de);
bool HandleNShieldAndTransparency(u8 e);
bool HandleCantAttackSubstatus(void);            /* text in gTextID */
bool HandleAmnesiaSubstatus(void);               /* text in gTextID */
bool HandleSandAttackOrSmokescreenSubstatus(void);
bool CheckSandAttackOrSmokescreenSubstatus(void);
bool HandleNoDamageOrEffectSubstatus(void);      /* text in gTextID */
bool HandleTransparency(void);
bool CheckNoDamageOrEffect(void);                /* text in gTextID (0 if already shown) */
bool IsClairvoyanceActive(void);
bool CheckIsIncapableOfUsingPkmnPower_ArenaCard(void); /* text in gTextID */
bool CheckIsIncapableOfUsingPkmnPower(u8 loc);   /* text in gTextID */
u8 CountPokemonWithActivePkmnPowerInBothPlayAreas(u8 id);   /* nonzero == carry */
u8 CountTurnDuelistPokemonWithActivePkmnPower(u8 id);       /* nonzero == carry */
u8 GetLoadedCard1RetreatCost(void);
bool CheckUnableToRetreatDueToEffect(void);      /* text in gTextID */
bool CheckCantUseTrainerDueToEffect(void);       /* text in gTextID */
bool IsPrehistoricPowerActive(void);             /* text in gTextID */
void ClearDamageReductionSubstatus2(void);
void UpdateSubstatusConditions_StartOfTurn(void);
void UpdateSubstatusConditions_EndOfTurn(void);
bool IsRainDanceActive(void);
bool CheckRainDanceScenario(void);
void HandleDestinyBondSubstatus(void);
void HandleStrikesBack_AgainstResidualAttack(void);
bool ApplyStrikesBack_AgainstResidualAttack(u16 damage);
void ClearChangedTypesIfMuk(u8 card);

/* ---------------------------------------------------------------- home misc (home_misc.c) */
void LoadCardDataToBuffer1_FromName(u16 name);
bool LoadCardDataToBuffer2_FromCardID(u8 e);
bool LoadCardDataToBuffer1_FromCardID(u8 e);
u8 GetCardType(u8 e);
u16 GetCardName(u8 e);
u8 GetCardTypeRarityAndSet(u8 id, u8 *rarity, u8 *set);
u8 GetArenaCardColor(void);
u8 GetPlayAreaCardColor(u8 e);
u8 GetPlayAreaCardWeakness(u8 loc);
u8 GetArenaCardWeakness(void);
u8 GetPlayAreaCardResistance(u8 loc);
u8 GetArenaCardResistance(void);
void HandleEnergyBurn(void);
void AddToDamage(u8 a);
void SubtractFromDamage(u8 a);
u8 ATimes10(u8 a);
u16 HLTimes10(u16 hl);
bool CompareDEtoBC(u16 de, u16 bc);
u16 HtimesL(u8 h, u8 l);
u8 Random(u8 a);
u8 UpdateRNGSources(void);
const struct EffectCommandList *GetEffectCommandList(u16 address);
EffectFunc CheckMatchingCommand(u16 list, u8 type);   /* NULL == carry */
EffectFunc GetEffectCommandFunction(u16 list, u8 type);
bool TryExecuteEffectCommandFunction(u8 type);
bool LoadDeck(u8 deck);

/* ---------------------------------------------------------------- home/serial.asm (link stubs, duel_core.c) */
/* The port has no serial link: the duelists of a "link" are both run locally
 * (see tcg.h). These keep the original's observable WRAM/HRAM writes. */
void ExchangeRNG(void);
void SetOppAction_SerialSendDuelData(u8 action);
void SerialSend8Bytes(void);
void SerialRecv8Bytes(void);

/* ---------------------------------------------------------------- home/ai.asm (ai/home_ai.c) */
void LoadOpponentDeck(void);
u8 AIDoAction_Turn(void);
u8 AIDoAction_StartDuel(void);
u8 AIDoAction_ForcedSwitch(void);                /* also stores the result in hTempPlayAreaLocation_ff9d */
u8 AIDoAction_KOSwitch(void);                    /* also stores the result in hTemp_ffa0 */
u8 AIDoAction_TakePrize(void);
u8 AIDoAction(u8 action);

/* ---------------------------------------------------------------- engine/duel/core.asm (duel_core.c) */
extern u8 gPlayerDeckCards[DECK_SIZE];           /* stands in for the selected SRAM deck (LoadPlayerDeck) */
void StartDuel_VSAIOpp(void);
void StartDuel(void);
bool DuelFinished(void);                         /* MainDuelLoop.duel_finished; true = sudden death follows */
bool DisplayRetreatScreen(void);
u8 PrintAndLoadAttacksToDuelTempList(void);
u8 IsLoadedCard1BasicPokemon(bool *carry);
u8 IsLoadedCard1BasicPokemon_SkipMysteriousFossilClefairyDoll(bool *carry);
bool DisplayPlaceInitialPokemonCardsScreen(u8 a, u16 text);
void DisplayNoBasicPokemonInHandScreenAndText(void);
void PrintReturnCardsToDeckDrawAgain(void);
bool DisplayPlayAreaScreenToUsePkmnPower(void);
void OppActionTable(u8 action);
void LoadCardNameToTxRam2(u8 card);
void LoadCardNameToTxRam2_b(u8 card);
void Func_6ba2(u16 text);
void PrintCardNameFromCardIDInTextBox(u16 text);
void ApplyStatusConditionToArenaPokemon(const u8 *entry);
bool ReplaceKnockedOutPokemon_Carry(void);
void TakeAPrizes(u8 a);
bool PracticeDuelAction(u8 action);              /* practice.c */
void LoadPlayerDeck(void);
bool CheckIfEnoughEnergiesToAttack(void);          /* attack at hCurMenuItem in wDuelTempList pairs; carry if not enough */
bool CheckIfEnoughEnergiesForGivenAttack(u8 d, u8 e);   /* d = deck index, e = attack index; uses wAttachedEnergies. carry if not enough */
bool CheckIfActiveCardParalyzedOrAsleep(void);   /* text in gTextID */
bool CheckAbleToRetreat(void);                   /* text in gTextID */
bool CheckIfEnoughEnergiesToRetreat(void);
bool AttemptRetreat(void);
void DiscardRetreatCostCards(void);
void ReturnRetreatCostCardsToArena(void);
bool HasAlivePokemonInBench(u8 *count);          /* carry if none; count = register b, may be NULL */
bool HasAlivePokemonInPlayArea(u8 *count);
bool GetCardOneStageBelow(u8 *d, u8 *e);         /* carry if basic; d = one stage below, e = card */
void ConvertSpecialTrainerCardToPokemon(u8 card, struct CardData *buf);
bool ApplyStatusConditionQueue(void);
void HandleDestinyBondAndBetweenTurnKnockOuts(void);
void HandleBetweenTurnKnockOuts(void);
void ClearNonTurnTemporaryDuelvars(void);
void ClearNonTurnTemporaryDuelvars_CopyStatus(void);
void UpdateArenaCardLastTurnDamage(void);
u16 PrintThereWasNoEffectFromStatusText(void);   /* returns the text id (hl) */
bool TurnDuelistTakePrizes(void);
void SaveDuelData(void);
bool AIMakeDecision(u8 action);                  /* OPPACTION_*; carry if the turn ended */
void InitVariablesToBeginDuel(void);
void InitVariablesToBeginTurn(void);
void SetAllPlayAreaPokemonCanEvolve(void);
u8 CountKnockedOutPokemon(void);
bool CheckIfTurnDuelistPlayAreaPokemonAreAllKnockedOut(void);
void ReplaceKnockedOutPokemon(void);
void MoveAllTurnHolderKnockedOutPokemonToDiscardPile(void);

/* engine/duel/effect_functions.asm helpers shared across files: see effects/effects.h */

/* ---------------------------------------------------------------- frontend glue (tcg.c, duel_ui.c) */
/* AI-vs-AI mode: AIDoAction uses the turn duelist's own deck AI (see ai/home_ai.c) */
extern bool gPerSideAI;
void host_save_duel(void);

#endif
