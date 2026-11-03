#include "cat_duo.h"
#include "gen/constants.h"
#include <assert.h>

void cat_duo_deck(uint8_t cards[60])
{
    static const uint8_t list[][2] = {
        {3,MEOWTH_LV15},{1,MEOWTH_LV14},{3,PERSIAN},
        {4,ELECTABUZZ_LV35},{2,SCYTHER},{1,MEW_LV8},
        {1,MEW_LV23},{1,KANGASKHAN},
        {4,BILL},{3,PROFESSOR_OAK},{3,COMPUTER_SEARCH},{2,ITEM_FINDER},
        {4,ENERGY_REMOVAL},{2,SUPER_ENERGY_REMOVAL},{3,GUST_OF_WIND},
        {3,PLUSPOWER},{2,SCOOP_UP},{2,SWITCH},
        {8,LIGHTNING_ENERGY},{4,PSYCHIC_ENERGY},{4,DOUBLE_COLORLESS_ENERGY}
    };
    unsigned n=0;
    for (unsigned i=0;i<sizeof(list)/sizeof(*list);i++)
        for (unsigned j=0;j<list[i][0];j++) {
            assert(n<60);
            cards[n++]=list[i][1];
        }
    assert(n==60);
}
