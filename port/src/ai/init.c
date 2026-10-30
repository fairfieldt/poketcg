/*
 * Port of src/engine/duel/ai/init.asm (INCLUDEd in engine/duel/ai/core.asm).
 */
#include "ai/ai.h"

void InitAIDuelVars(void)
{
	TRACE_FN();
	ClearMemory_Bank5((u8)(ADDR_wAIDuelVarsEnd - ADDR_wAIDuelVars), &wAIDuelVars);
	wAIPokedexCounter = 5;
	wAIPeekedPrizes = 0xff;
}

/* initializes some variables and sets value of wAIBarrierFlagCounter.
 * if Player uses Barrier 3 times in a row, AI checks if Player's deck
 * has only MewtwoLv53 Pokemon cards (running a MewtwoLv53 mill deck). */
void InitAITurnVars(void)
{
	TRACE_FN();
	u8 a, e;

	/* increase Pokedex counter by 1 */
	wAIPokedexCounter = (u8)(wAIPokedexCounter + 1);

	wPreviousAIFlags = 0;
	wAITriedAttack = 0;
	wUnused_cddc = 0;
	wAIRetreatedThisTurn = 0;

	/* checks if the Player used an attack last turn
	 * and if it was the second attack of their card. */
	a = wPlayerAttackingAttackIndex;
	if (a == 0xff)
		goto check_flag;
	if (a == 0)
		goto check_flag;
	a = wPlayerAttackingCardIndex;
	if (a == 0xff)
		goto check_flag;

	/* if the card is MewtwoLv53, it means the Player
	 * used its second attack, Barrier. */
	SwapTurn();
	e = GetCardIDFromDeckIndex(a);
	SwapTurn();
	if (e != MEWTWO_LV53)
		goto check_flag;
	/* Player used Barrier last turn */

	/* check if flag was already set, if so,
	 * reset wAIBarrierFlagCounter to $80. */
	a = wAIBarrierFlagCounter;
	if (a & (1 << AI_MEWTWO_MILL_F))
		goto set_flag;

	/* if not, increase it by 1 and check if it exceeds 2. */
	a = (u8)(a + 1);
	wAIBarrierFlagCounter = a;
	if (a < 3)
		return;

	/* this means that the Player used Barrier
	 * at least 3 turns in a row.
	 * check if Player is running MewtwoLv53-only deck,
	 * if so, set wAIBarrierFlagCounter flag. */
	a = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD);
	SwapTurn();
	e = GetCardIDFromDeckIndex(a);
	SwapTurn();
	if (e != MEWTWO_LV53)
		goto reset_1;
	if (!CheckIfPlayerHasPokemonOtherThanMewtwoLv53())
		goto set_flag;
reset_1:
	/* reset wAIBarrierFlagCounter */
	wAIBarrierFlagCounter = 0;
	return;

set_flag:
	wAIBarrierFlagCounter = AI_MEWTWO_MILL;
	return;

check_flag:
	/* increase counter by 1 if flag is set */
	a = wAIBarrierFlagCounter;
	if (!(a & (1 << AI_MEWTWO_MILL_F)))
		goto reset_2;
	wAIBarrierFlagCounter = (u8)(a + 1);
	return;

reset_2:
	/* reset wAIBarrierFlagCounter */
	wAIBarrierFlagCounter = 0;
	/* .done */
}
