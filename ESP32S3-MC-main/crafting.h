#ifndef CRAFTING_H
#define CRAFTING_H

#include "game_types.h"

void getCraftingOutput(PlayerData *player, uint8_t *count, uint16_t *item);
void getSmeltingOutput(PlayerData *player);

#endif
