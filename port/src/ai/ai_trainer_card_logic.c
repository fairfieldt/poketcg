/*
 * Port of src/data/duel/ai_trainer_card_logic.asm, walked by _AIProcessHandTrainerCards.
 *
 * MACRO ai_trainer_card_logic
 *	db \1 ; AI_TRAINER_CARD_PHASE_* constant
 *	db \2 ; ID of trainer card
 *	dw \3 ; function for AI decision to play card
 *	dw \4 ; function for AI playing the card
 */
#include "ai/ai.h"

const struct AITrainerCardLogicEntry AITrainerCardLogic[NUM_AI_TRAINER_CARD_LOGIC + 1] = {
	{ AI_TRAINER_CARD_PHASE_07, POTION,                 AIDecide_Potion_Phase07,                 AIPlay_Potion },
	{ AI_TRAINER_CARD_PHASE_10, POTION,                 AIDecide_Potion_Phase10,                 AIPlay_Potion },
	{ AI_TRAINER_CARD_PHASE_08, SUPER_POTION,           AIDecide_SuperPotion_Phase08,            AIPlay_SuperPotion },
	{ AI_TRAINER_CARD_PHASE_11, SUPER_POTION,           AIDecide_SuperPotion_Phase11,            AIPlay_SuperPotion },
	{ AI_TRAINER_CARD_PHASE_13, DEFENDER,               AIDecide_Defender_Phase13,               AIPlay_Defender },
	{ AI_TRAINER_CARD_PHASE_14, DEFENDER,               AIDecide_Defender_Phase14,               AIPlay_Defender },
	{ AI_TRAINER_CARD_PHASE_13, PLUSPOWER,              AIDecide_PlusPower_Phase13,              AIPlay_PlusPower },
	{ AI_TRAINER_CARD_PHASE_14, PLUSPOWER,              AIDecide_PlusPower_Phase14,              AIPlay_PlusPower },
	{ AI_TRAINER_CARD_PHASE_09, SWITCH,                 AIDecide_Switch,                         AIPlay_Switch },
	{ AI_TRAINER_CARD_PHASE_07, GUST_OF_WIND,           AIDecide_GustOfWind,                     AIPlay_GustOfWind },
	{ AI_TRAINER_CARD_PHASE_10, GUST_OF_WIND,           AIDecide_GustOfWind,                     AIPlay_GustOfWind },
	{ AI_TRAINER_CARD_PHASE_04, BILL,                   AIDecide_Bill,                           AIPlay_Bill },
	{ AI_TRAINER_CARD_PHASE_05, ENERGY_REMOVAL,         AIDecide_EnergyRemoval,                  AIPlay_EnergyRemoval },
	{ AI_TRAINER_CARD_PHASE_05, SUPER_ENERGY_REMOVAL,   AIDecide_SuperEnergyRemoval,             AIPlay_SuperEnergyRemoval },
	{ AI_TRAINER_CARD_PHASE_07, POKEMON_BREEDER,        AIDecide_PokemonBreeder,                 AIPlay_PokemonBreeder },
	{ AI_TRAINER_CARD_PHASE_15, PROFESSOR_OAK,          AIDecide_ProfessorOak,                   AIPlay_ProfessorOak },
	{ AI_TRAINER_CARD_PHASE_10, ENERGY_RETRIEVAL,       AIDecide_EnergyRetrieval,                AIPlay_EnergyRetrieval },
	{ AI_TRAINER_CARD_PHASE_11, SUPER_ENERGY_RETRIEVAL, AIDecide_SuperEnergyRetrieval,           AIPlay_SuperEnergyRetrieval },
	{ AI_TRAINER_CARD_PHASE_06, POKEMON_CENTER,         AIDecide_PokemonCenter,                  AIPlay_PokemonCenter },
	{ AI_TRAINER_CARD_PHASE_07, IMPOSTER_PROFESSOR_OAK, AIDecide_ImposterProfessorOak,           AIPlay_ImposterProfessorOak },
	{ AI_TRAINER_CARD_PHASE_12, ENERGY_SEARCH,          AIDecide_EnergySearch,                   AIPlay_EnergySearch },
	{ AI_TRAINER_CARD_PHASE_03, POKEDEX,                AIDecide_Pokedex,                        AIPlay_Pokedex },
	{ AI_TRAINER_CARD_PHASE_07, FULL_HEAL,              AIDecide_FullHeal,                       AIPlay_FullHeal },
	{ AI_TRAINER_CARD_PHASE_10, MR_FUJI,                AIDecide_MrFuji,                         AIPlay_MrFuji },
	{ AI_TRAINER_CARD_PHASE_10, SCOOP_UP,               AIDecide_ScoopUp,                        AIPlay_ScoopUp },
	{ AI_TRAINER_CARD_PHASE_02, MAINTENANCE,            AIDecide_Maintenance,                    AIPlay_Maintenance },
	{ AI_TRAINER_CARD_PHASE_03, RECYCLE,                AIDecide_Recycle,                        AIPlay_Recycle },
	{ AI_TRAINER_CARD_PHASE_13, LASS,                   AIDecide_Lass,                           AIPlay_Lass },
	{ AI_TRAINER_CARD_PHASE_04, ITEM_FINDER,            AIDecide_ItemFinder,                     AIPlay_ItemFinder },
	{ AI_TRAINER_CARD_PHASE_01, IMAKUNI_CARD,           AIDecide_Imakuni,                        AIPlay_Imakuni },
	{ AI_TRAINER_CARD_PHASE_01, GAMBLER,                AIDecide_Gambler,                        AIPlay_Gambler },
	{ AI_TRAINER_CARD_PHASE_05, REVIVE,                 AIDecide_Revive,                         AIPlay_Revive },
	{ AI_TRAINER_CARD_PHASE_13, POKEMON_FLUTE,          AIDecide_PokemonFlute,                   AIPlay_PokemonFlute },
	{ AI_TRAINER_CARD_PHASE_05, CLEFAIRY_DOLL,          AIDecide_ClefairyDollOrMysteriousFossil, AIPlay_ClefairyDollOrMysteriousFossil },
	{ AI_TRAINER_CARD_PHASE_05, MYSTERIOUS_FOSSIL,      AIDecide_ClefairyDollOrMysteriousFossil, AIPlay_ClefairyDollOrMysteriousFossil },
	{ AI_TRAINER_CARD_PHASE_02, POKE_BALL,              AIDecide_Pokeball,                       AIPlay_Pokeball },
	{ AI_TRAINER_CARD_PHASE_02, COMPUTER_SEARCH,        AIDecide_ComputerSearch,                 AIPlay_ComputerSearch },
	{ AI_TRAINER_CARD_PHASE_02, POKEMON_TRADER,         AIDecide_PokemonTrader,                  AIPlay_PokemonTrader },
	{ 0xff, 0, NULL, NULL }, /* db $ff */
};
