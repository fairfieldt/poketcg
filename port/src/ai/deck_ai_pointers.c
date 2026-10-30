/*
 * Port of src/data/deck_ai_pointers.asm: the deck AI action table of every deck ID,
 * indexed by wOpponentDeckID (see AIDoAction in ai/home_ai.c).
 */
#include "ai/ai.h"

const AIActionFunc *const DeckAIPointerTable[NUM_DECK_IDS + 1] = {
	AIActionTable_SamPractice,         /* SAMS_PRACTICE_DECK */
	AIActionTable_GeneralDecks,        /* PRACTICE_PLAYER_DECK */
	AIActionTable_GeneralDecks,        /* SAMS_NORMAL_DECK */
	AIActionTable_GeneralDecks,        /* CHARMANDER_AND_FRIENDS_DECK */
	AIActionTable_GeneralDecks,        /* CHARMANDER_EXTRA_DECK */
	AIActionTable_GeneralDecks,        /* SQUIRTLE_AND_FRIENDS_DECK */
	AIActionTable_GeneralDecks,        /* SQUIRTLE_EXTRA_DECK */
	AIActionTable_GeneralDecks,        /* BULBASAUR_AND_FRIENDS_DECK */
	AIActionTable_GeneralDecks,        /* BULBASAUR_EXTRA_DECK */
	AIActionTable_GeneralDecks,        /* LIGHTNING_AND_FIRE_DECK */
	AIActionTable_GeneralDecks,        /* WATER_AND_FIGHTING_DECK */
	AIActionTable_GeneralDecks,        /* GRASS_AND_PSYCHIC_DECK */
	AIActionTable_LegendaryMoltres,    /* LEGENDARY_MOLTRES_DECK */
	AIActionTable_LegendaryZapdos,     /* LEGENDARY_ZAPDOS_DECK */
	AIActionTable_LegendaryArticuno,   /* LEGENDARY_ARTICUNO_DECK */
	AIActionTable_LegendaryDragonite,  /* LEGENDARY_DRAGONITE_DECK */
	AIActionTable_FirstStrike,         /* FIRST_STRIKE_DECK */
	AIActionTable_RockCrusher,         /* ROCK_CRUSHER_DECK */
	AIActionTable_GoGoRainDance,       /* GO_GO_RAIN_DANCE_DECK */
	AIActionTable_ZappingSelfdestruct, /* ZAPPING_SELFDESTRUCT_DECK */
	AIActionTable_FlowerPower,         /* FLOWER_POWER_DECK */
	AIActionTable_StrangePsyshock,     /* STRANGE_PSYSHOCK_DECK */
	AIActionTable_WondersOfScience,    /* WONDERS_OF_SCIENCE_DECK */
	AIActionTable_FireCharge,          /* FIRE_CHARGE_DECK */
	AIActionTable_ImRonald,            /* IM_RONALD_DECK */
	AIActionTable_PowerfulRonald,      /* POWERFUL_RONALD_DECK */
	AIActionTable_InvincibleRonald,    /* INVINCIBLE_RONALD_DECK */
	AIActionTable_LegendaryRonald,     /* LEGENDARY_RONALD_DECK */
	AIActionTable_GeneralNoRetreat,    /* MUSCLES_FOR_BRAINS_DECK */
	AIActionTable_GeneralDecks,        /* HEATED_BATTLE_DECK */
	AIActionTable_GeneralDecks,        /* LOVE_TO_BATTLE_DECK */
	AIActionTable_GeneralDecks,        /* EXCAVATION_DECK */
	AIActionTable_GeneralDecks,        /* BLISTERING_POKEMON_DECK */
	AIActionTable_GeneralDecks,        /* HARD_POKEMON_DECK */
	AIActionTable_GeneralDecks,        /* WATERFRONT_POKEMON_DECK */
	AIActionTable_GeneralDecks,        /* LONELY_FRIENDS_DECK */
	AIActionTable_GeneralDecks,        /* SOUND_OF_THE_WAVES_DECK */
	AIActionTable_GeneralDecks,        /* PIKACHU_DECK */
	AIActionTable_GeneralDecks,        /* BOOM_BOOM_SELFDESTRUCT_DECK */
	AIActionTable_GeneralDecks,        /* POWER_GENERATOR_DECK */
	AIActionTable_GeneralDecks,        /* ETCETERA_DECK */
	AIActionTable_GeneralDecks,        /* FLOWER_GARDEN_DECK */
	AIActionTable_GeneralDecks,        /* KALEIDOSCOPE_DECK */
	AIActionTable_GeneralDecks,        /* GHOST_DECK */
	AIActionTable_GeneralDecks,        /* NAP_TIME_DECK */
	AIActionTable_GeneralDecks,        /* STRANGE_POWER_DECK */
	AIActionTable_GeneralDecks,        /* FLYIN_POKEMON_DECK */
	AIActionTable_GeneralDecks,        /* LOVELY_NIDORAN_DECK */
	AIActionTable_GeneralDecks,        /* POISON_DECK */
	AIActionTable_GeneralDecks,        /* ANGER_DECK */
	AIActionTable_GeneralDecks,        /* FLAMETHROWER_DECK */
	AIActionTable_GeneralDecks,        /* RESHUFFLE_DECK */
	AIActionTable_GeneralNoRetreat,    /* IMAKUNI_DECK */
	/* assert_table_length NUM_DECK_IDS */
	/* one past the end (wOpponentDeckID == NUM_DECK_IDS passes LoadOpponentDeck's check):
	 * the original reads the first word of AIActionTable_Unreferenced, which follows this
	 * table in ROM: dw $406c = AIActionTable_Unreferenced + 2 */
	&AIActionTable_Unreferenced[1],
};
