#include "crafting.h"
#include "game_state.h"
#include "registries.h"
#include <string.h>

void getCraftingOutput(PlayerData *player, uint8_t *count, uint16_t *item) {
    if (player->flags & 0x80) { *count = 0; *item = 0; return; }

    uint8_t i, filled = 0, first = 10, identical = 1;
    for (i = 0; i < 9; i++) {
        if (player->craft_items[i]) {
            filled++;
            if (first == 10) first = i;
            else if (player->craft_items[i] != player->craft_items[first]) identical = 0;
        }
    }

    if (filled == 0) {
        *item = 0;
        *count = 0;
        return;
    }

    uint16_t first_item = player->craft_items[first];
    uint8_t first_col = first % 3, first_row = first / 3;

    switch (filled) {
        // ====== 1个物品 ======
        case 1:
            switch (first_item) {
                case I_oak_log: *item = I_oak_planks; *count = 4; return;
                case I_oak_planks: *item = I_oak_button; *count = 1; return;
                case I_iron_block: *item = I_iron_ingot; *count = 9; return;
                case I_gold_block: *item = I_gold_ingot; *count = 9; return;
                case I_diamond_block: *item = I_diamond; *count = 9; return;
                case I_redstone_block: *item = I_redstone; *count = 9; return;
                case I_coal_block: *item = I_coal; *count = 9; return;
                case I_copper_block: *item = I_copper_ingot; *count = 9; return;
                default: break;
            }
            break;

        // ====== 2个物品 ======
        case 2:
            if (first_item == I_oak_planks) {
                if (first_col != 2 && player->craft_items[first + 1] == I_oak_planks) {
                    *item = I_oak_pressure_plate; *count = 1; return;
                }
                if (first_row != 2 && player->craft_items[first + 3] == I_oak_planks) {
                    *item = I_stick; *count = 4; return;
                }
            }
            if ((first_item == I_charcoal || first_item == I_coal) && 
                first_row != 2 && player->craft_items[first + 3] == I_stick) {
                *item = I_torch; *count = 4; return;
            }
            if (first_item == I_iron_ingot && (
                (first_row != 2 && first_col != 2 && player->craft_items[first + 4] == I_iron_ingot) ||
                (first_row != 2 && first_col != 0 && player->craft_items[first + 2] == I_iron_ingot)
            )) {
                *item = I_shears; *count = 1; return;
            }
            break;

        // ====== 3个物品（台阶、铲子、剑、弓、箭、水桶） ======
        case 3:
            // 台阶
            if ((first_item == I_oak_planks || first_item == I_cobblestone ||
                 first_item == I_stone) &&
                first_col == 0 && player->craft_items[first + 1] == first_item &&
                player->craft_items[first + 2] == first_item) {
                if (first_item == I_oak_planks) *item = I_oak_slab;
                else if (first_item == I_cobblestone) *item = I_cobblestone_slab;
                else if (first_item == I_stone) *item = I_stone_slab;
                *count = 6;
                return;
            }
            // 铲子
            if (first_row == 0 && player->craft_items[first + 3] == I_stick &&
                player->craft_items[first + 6] == I_stick) {
                if (first_item == I_oak_planks) { *item = I_wooden_shovel; *count = 1; return; }
                if (first_item == I_cobblestone) { *item = I_stone_shovel; *count = 1; return; }
                if (first_item == I_iron_ingot) { *item = I_iron_shovel; *count = 1; return; }
                if (first_item == I_gold_ingot) { *item = I_golden_shovel; *count = 1; return; }
                if (first_item == I_diamond) { *item = I_diamond_shovel; *count = 1; return; }
            }
            // 剑
            if (first_row == 0 && player->craft_items[first + 3] == first_item &&
                player->craft_items[first + 6] == I_stick) {
                if (first_item == I_oak_planks) { *item = I_wooden_sword; *count = 1; return; }
                if (first_item == I_cobblestone) { *item = I_stone_sword; *count = 1; return; }
                if (first_item == I_iron_ingot) { *item = I_iron_sword; *count = 1; return; }
                if (first_item == I_gold_ingot) { *item = I_golden_sword; *count = 1; return; }
                if (first_item == I_diamond) { *item = I_diamond_sword; *count = 1; return; }
            }
            // ====== 弓 ======
    if (player->craft_items[0] == 0 &&
        player->craft_items[1] == I_stick &&
        player->craft_items[2] == I_string &&
        player->craft_items[3] == I_stick &&
        player->craft_items[4] == 0 &&
        player->craft_items[5] == I_string &&
        player->craft_items[6] == 0 &&
        player->craft_items[7] == I_stick &&
        player->craft_items[8] == I_string) {
        *item = I_bow;
        *count = 1;
        return;
    }
            // 箭
            if (first_item == I_flint && first_col == 0 && first_row == 0 &&
                player->craft_items[first + 1] == I_stick && player->craft_items[first + 2] == I_feather) {
                *item = I_arrow;
                *count = 4;
                return;
            }
            
            // ====== 水桶（3个铁锭，V字形） ======
    if (player->craft_items[3] == I_iron_ingot &&
        player->craft_items[5] == I_iron_ingot &&
        player->craft_items[7] == I_iron_ingot &&
        player->craft_items[0] == 0 &&
        player->craft_items[1] == 0 &&
        player->craft_items[2] == 0 &&
        player->craft_items[4] == 0 &&
        player->craft_items[6] == 0 &&
        player->craft_items[8] == 0) {
        *item = I_water_bucket;
        *count = 1;
        return;
    }
            break;

        // ====== 4个物品（2x2配方 + 靴子） ======
        case 4:
            // 2x2 配方
            if (first_col != 2 && first_row != 2 &&
                player->craft_items[first + 1] == first_item &&
                player->craft_items[first + 3] == first_item &&
                player->craft_items[first + 4] == first_item) {
                if (first_item == I_oak_planks) {
                    *item = I_crafting_table;
                    *count = 1;
                    return;
                }
                if (first_item == I_oak_log) {
                    *item = I_oak_wood;
                    *count = 3;
                    return;
                }
                if (first_item == I_snowball) {
                    *item = I_snow_block;
                    *count = 1;
                    return;
                }
            }
            
            // ====== 靴子（4个材料） ======
            // 位置 3,5,6,8
            if (player->craft_items[3] == first_item &&
                player->craft_items[5] == first_item &&
                player->craft_items[6] == first_item &&
                player->craft_items[8] == first_item &&
                player->craft_items[0] == 0 &&
                player->craft_items[1] == 0 &&
                player->craft_items[2] == 0 &&
                player->craft_items[4] == 0 &&
                player->craft_items[7] == 0) {
                if (first_item == I_leather) { *item = I_leather_boots; *count = 1; return; }
                if (first_item == I_iron_ingot) { *item = I_iron_boots; *count = 1; return; }
                if (first_item == I_gold_ingot) { *item = I_golden_boots; *count = 1; return; }
                if (first_item == I_diamond) { *item = I_diamond_boots; *count = 1; return; }
                if (first_item == I_netherite_ingot) { *item = I_netherite_boots; *count = 1; return; }
            }
            break;

        // ====== 5个物品（镐子、斧子、头盔） ======
        case 5:
            // 镐子
            if (first == 0 && player->craft_items[1] == first_item && player->craft_items[2] == first_item &&
                player->craft_items[4] == I_stick && player->craft_items[7] == I_stick) {
                if (first_item == I_oak_planks) { *item = I_wooden_pickaxe; *count = 1; return; }
                if (first_item == I_cobblestone) { *item = I_stone_pickaxe; *count = 1; return; }
                if (first_item == I_iron_ingot) { *item = I_iron_pickaxe; *count = 1; return; }
                if (first_item == I_gold_ingot) { *item = I_golden_pickaxe; *count = 1; return; }
                if (first_item == I_diamond) { *item = I_diamond_pickaxe; *count = 1; return; }
            }
            // 斧子
            if (first < 2 && player->craft_items[first + 1] == first_item &&
                ((player->craft_items[first + 3] == first_item && player->craft_items[first + 4] == I_stick && player->craft_items[first + 7] == I_stick) ||
                 (player->craft_items[first + 4] == first_item && player->craft_items[first + 3] == I_stick && player->craft_items[first + 6] == I_stick))) {
                if (first_item == I_oak_planks) { *item = I_wooden_axe; *count = 1; return; }
                if (first_item == I_cobblestone) { *item = I_stone_axe; *count = 1; return; }
                if (first_item == I_iron_ingot) { *item = I_iron_axe; *count = 1; return; }
                if (first_item == I_gold_ingot) { *item = I_golden_axe; *count = 1; return; }
                if (first_item == I_diamond) { *item = I_diamond_axe; *count = 1; return; }
            }
            
            // ====== 头盔（5个材料） ======
            // 位置 0,1,2,3,5
            if (player->craft_items[0] == first_item &&
                player->craft_items[1] == first_item &&
                player->craft_items[2] == first_item &&
                player->craft_items[3] == first_item &&
                player->craft_items[5] == first_item &&
                player->craft_items[4] == 0 &&
                player->craft_items[6] == 0 &&
                player->craft_items[7] == 0 &&
                player->craft_items[8] == 0) {
                if (first_item == I_leather) { *item = I_leather_helmet; *count = 1; return; }
                if (first_item == I_iron_ingot) { *item = I_iron_helmet; *count = 1; return; }
                if (first_item == I_gold_ingot) { *item = I_golden_helmet; *count = 1; return; }
                if (first_item == I_diamond) { *item = I_diamond_helmet; *count = 1; return; }
                if (first_item == I_netherite_ingot) { *item = I_netherite_helmet; *count = 1; return; }
            }
            break;

        // ====== 6个物品（原 case 6 保留给其他配方） ======
        case 6:
            // 这里可以放其他6个物品的配方
            break;

        // ====== 7个物品（护腿 + 盾牌） ======
        case 7:
            // ====== 护腿（7个材料） ======
            if (player->craft_items[0] == first_item &&
                player->craft_items[1] == first_item &&
                player->craft_items[2] == first_item &&
                player->craft_items[3] == first_item &&
                player->craft_items[5] == first_item &&
                player->craft_items[6] == first_item &&
                player->craft_items[8] == first_item &&
                player->craft_items[4] == 0 &&
                player->craft_items[7] == 0) {
                if (first_item == I_leather) { *item = I_leather_leggings; *count = 1; return; }
                if (first_item == I_iron_ingot) { *item = I_iron_leggings; *count = 1; return; }
                if (first_item == I_gold_ingot) { *item = I_golden_leggings; *count = 1; return; }
                if (first_item == I_diamond) { *item = I_diamond_leggings; *count = 1; return; }
                if (first_item == I_netherite_ingot) { *item = I_netherite_leggings; *count = 1; return; }
            }
            
            // ====== 盾牌（6木板 + 1铁锭） ======
            if (player->craft_items[0] == I_oak_planks &&
                player->craft_items[1] == I_iron_ingot &&
                player->craft_items[2] == I_oak_planks &&
                player->craft_items[3] == I_oak_planks &&
                player->craft_items[4] == I_oak_planks &&
                player->craft_items[5] == I_oak_planks &&
                player->craft_items[6] == 0 &&
                player->craft_items[7] == I_oak_planks &&
                player->craft_items[8] == 0) {
                *item = I_shield;
                *count = 1;
                return;
            }
            break;

        // ====== 8个物品（熔炉 + 下界合金锭 + 胸甲） ======
        case 8: {
            // ====== 熔炉 ======
            if (identical && player->craft_items[4] == 0) {
                if (first_item == I_cobblestone) {
                    *item = I_furnace;
                    *count = 1;
                    return;
                }
#ifdef ALLOW_CHESTS
                if (first_item == I_oak_planks) {
                    *item = I_chest;
                    *count = 1;
                    return;
                }
#endif
            }
            
            // ====== 下界合金锭（4碎片 + 4金锭） ======
            int scrap_count = 0;
            int gold_count = 0;
            for (int j = 0; j < 9; j++) {
                if (player->craft_items[j] == I_netherite_scrap) scrap_count++;
                if (player->craft_items[j] == I_gold_ingot) gold_count++;
            }
            if (scrap_count == 4 && gold_count == 4) {
                if (player->craft_items[4] != 0) {
                    *item = I_netherite_ingot;
                    *count = 1;
                    return;
                }
            }
            
            // ====== 胸甲（8个材料） ======
            if (player->craft_items[0] == first_item &&
                player->craft_items[1] == 0 &&
                player->craft_items[2] == first_item &&
                player->craft_items[3] == first_item &&
                player->craft_items[4] == first_item &&
                player->craft_items[5] == first_item &&
                player->craft_items[6] == first_item &&
                player->craft_items[7] == first_item &&
                player->craft_items[8] == first_item) {
                if (first_item == I_leather) { *item = I_leather_chestplate; *count = 1; return; }
                if (first_item == I_iron_ingot) { *item = I_iron_chestplate; *count = 1; return; }
                if (first_item == I_gold_ingot) { *item = I_golden_chestplate; *count = 1; return; }
                if (first_item == I_diamond) { *item = I_diamond_chestplate; *count = 1; return; }
                if (first_item == I_netherite_ingot) { *item = I_netherite_chestplate; *count = 1; return; }
            }
            break;
        }

        // ====== 9个物品（方块压缩） ======
        case 9:
            if (identical) {
                if (first_item == I_iron_ingot) { *item = I_iron_block; *count = 1; return; }
                if (first_item == I_gold_ingot) { *item = I_gold_block; *count = 1; return; }
                if (first_item == I_diamond) { *item = I_diamond_block; *count = 1; return; }
                if (first_item == I_redstone) { *item = I_redstone_block; *count = 1; return; }
                if (first_item == I_coal) { *item = I_coal_block; *count = 1; return; }
                if (first_item == I_copper_ingot) { *item = I_copper_block; *count = 1; return; }
            }
            break;

        default:
            break;
    }

    *count = 0;
    *item = 0;
}

#define registerSmeltingRecipe(a, b) \
    if (*material == a && (*output_item == b || *output_item == 0)) *output_item = b

void getSmeltingOutput(PlayerData *player) {
    uint8_t *material_count = &player->craft_count[0];
    uint8_t *fuel_count = &player->craft_count[1];
    if (*material_count == 0 || *fuel_count == 0) return;

    uint16_t *material = &player->craft_items[0];
    uint16_t *fuel = &player->craft_items[1];
    if (*material == 0 || *fuel == 0) return;

    uint8_t *output_count = &player->craft_count[2];
    uint16_t *output_item = &player->craft_items[2];

    uint8_t fuel_value = 0;
    if (*fuel == I_coal || *fuel == I_charcoal) fuel_value = 8;
    else if (*fuel == I_coal_block) fuel_value = 80;
    else if (*fuel == I_oak_planks || *fuel == I_oak_log || *fuel == I_crafting_table) fuel_value = 1 + (fast_rand() & 1);
    else if (*fuel == I_stick || *fuel == I_oak_sapling) fuel_value = (fast_rand() & 1);
    else return;

    uint8_t exchange = *material_count > fuel_value ? fuel_value : *material_count;

    registerSmeltingRecipe(I_cobblestone, I_stone);
    else registerSmeltingRecipe(I_oak_log, I_charcoal);
    else registerSmeltingRecipe(I_oak_wood, I_charcoal);
    else registerSmeltingRecipe(I_raw_iron, I_iron_ingot);
    else registerSmeltingRecipe(I_raw_gold, I_gold_ingot);
    else registerSmeltingRecipe(I_sand, I_glass);
    else registerSmeltingRecipe(I_chicken, I_cooked_chicken);
    else registerSmeltingRecipe(I_beef, I_cooked_beef);
    else registerSmeltingRecipe(I_porkchop, I_cooked_porkchop);
    else registerSmeltingRecipe(I_mutton, I_cooked_mutton);
    else return;

    *output_count += exchange;
    *material_count -= exchange;
    *fuel_count -= 1;
    if (*fuel_count == 0) *fuel = 0;
    if (*material_count <= 0) { *material_count = 0; *material = 0; }
    else getSmeltingOutput(player);
}
