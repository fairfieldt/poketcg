/*
 * Shared helpers of engine/duel/effect_functions.asm that are used across the
 * four parts of the port (effects/part1.c .. part4.c). Everything else defined
 * in effect_functions.asm is either an effect function (declared in
 * gen/effect_commands.h with the signature `bool Name(void)`, the return value
 * being the carry flag) or a static helper of the part that defines it.
 *
 * Signatures follow PORTING.md. "carry" = true return value. Text IDs that the
 * original returns in hl are put in gTextID.
 */
#ifndef TCG_EFFECTS_H
#define TCG_EFFECTS_H

#include "../engine.h"

/* ---- part 1 (effect_functions.asm lines 1-2799) ---- */
bool QueueStatusCondition(u8 b, u8 c);       /* b = mask of conditions to keep, c = condition; carry if queued */
bool TossCoin_BankB(u16 text);               /* carry = heads */
u8 TossCoinATimes_BankB(u8 n, u16 text);     /* number of heads (carry if nonzero) */
bool Serial_TossCoin(u16 text);
u8 Serial_TossCoinATimes(u8 n, u16 text);
void SetNoEffectFromStatus(void);
void SetWasUnsuccessful(void);
void ShuffleCardsInDeck(void);
bool IsPlayerTurn(void);                     /* carry if the turn duelist is DUELIST_TYPE_PLAYER */
void UpdateExpectedAIDamage_AccountForPoison(u8 a, u8 d, u8 e);
void UpdateExpectedAIDamage(u8 a, u8 d, u8 e);
void SetExpectedAIDamage(u8 a, u8 d, u8 e);  /* wDamage = a, wAIMinDamage = d, wAIMaxDamage = e */
void DrawPlayAreaScreenToShowChanges(u8 loc);
void DealDamageToAllBenchedPokemon(u8 damage);
void PlayAttackAnimationOverAttackingPokemon(u8 anim);
void ApplySubstatus1ToAttackingCard(u8 substatus);
void ApplySubstatus2ToDefendingCard(u8 substatus);
void SetDefiniteDamage(u8 damage);
void SetDefiniteAIDamage(void);
u8 PickRandomPlayAreaCard(void);
u8 *GetNextPositionInTempList(void);         /* &hTempList[hCurSelectionItem++] */
u8 CreateListOfFireEnergyAttachedToArena(void);   /* count; list in wDuelTempList */
u8 CreateListOfEnergyAttachedToArena(u8 type);    /* type = TYPE_ENERGY_*; count */
void PrintDevolvedCardNameAndLevelText(u8 d, u8 e); /* d = lower stage card, e = devolved card */
void HandleSwitchDefendingPokemonEffect(u8 loc);
bool HandleNoDamageOrEffect(void);           /* carry if the Defending Pokemon has No Damage or Effect (text printed) */
void ApplyAndAnimateHPRecovery(u16 hp);
bool CheckIfPlayAreaHasAnyDamage(void);      /* carry if no damage counters */
bool CreateTrainerCardListFromDiscardPile(void);  /* carry (and gTextID) if none */
bool CreateEnergyCardListFromDiscardPile_OnlyBasic(void);
bool CreateEnergyCardListFromDiscardPile_AllEnergy(void);
bool CheckIfDeckIsEmpty(void);               /* carry (and gTextID) if empty */
bool LookForCardsInDeck(u8 d, u8 e, u16 text); /* d = SEARCHEFFECT_*, e = card id; carry if refused to look */
bool HandleDefendingPokemonAttackSelection(u8 *d, u8 *e); /* carry if none; d = defending card, e = attack index */
u16 GetAttackName(u8 d, u8 e);               /* text id (loads wLoadedCard1) */
bool CheckIfDefendingPokemonHasAnyAttack(void); /* carry if none */
void UpdateDevolvedCardHPAndStage(u8 card);
void ResetDevolvedCardStatus(void);
bool AskWhetherToQuitSelectingCards(u8 total); /* carry if "No" */
u8 DuelistSelectForcedSwitch(void);          /* location also in hTempPlayAreaLocation_ff9d */
u8 AIPickEnergyCardToDiscardFromDefendingPokemon(void); /* deck index or $ff */
u8 AIPickAttackForAmnesia(void);             /* attack index */
u8 AIFindTargetForBenchAttack(void);         /* PLAY_AREA_* */
bool HandleColorChangeScreen(u8 loc, u16 text, u8 *color); /* carry if B */
void LoadCardNameAndInputColor(u8 color);
void DrawSymbolOnPlayAreaCursor(u8 a, u8 b); /* a = play area slot, b = symbol tile */
void ApplyExtraWaterEnergyDamageBonus(u8 b, u8 c);

/* ---- part 2 (lines 2800-5599) ---- */
void PlayerPickAttackForAmnesia(void);       /* result in hTemp_ffa0 */
void ApplyAmnesiaToAttack(void);

/* ---- part 3 (lines 5600-8399) ---- */
bool TryGiveDamageCounter_StrangeBehavior(void); /* carry if it would KO */

/* ---- part 4 (lines 8400-end) ---- */

#endif
