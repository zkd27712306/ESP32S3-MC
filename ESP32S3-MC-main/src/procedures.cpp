#include "procedures.h"
#include "game_state.h"
#include "registries.h"
#include "packet_codec.h"
#include "terrain.h"
#include <string.h>
#include <stdint.h>

// 槽位同步回调 (由 mc_server.cpp 注册)
void (*g_sync_slot_cb)(int fd, int slot, uint8_t count, uint16_t item) = nullptr;
// slot_idx -> 真实 socket fd 的映射表 (由 mc_server.cpp 维护)
int g_slot_fd_map[MAX_PLAYERS] = {-1, -1, -1, -1, -1};

// ============ 方块改动 ============
uint8_t getBlockChange(int16_t x, uint8_t y, int16_t z) {
  for (int i = 0; i < block_changes_count; i++) {
    if (block_changes[i].block == 0xFF) continue;
    if (block_changes[i].x == x && block_changes[i].y == y && block_changes[i].z == z)
      return block_changes[i].block;
  }
  return 0xFF;
}

uint8_t makeBlockChange(int16_t x, uint8_t y, int16_t z, uint8_t block) {
  // 先查找已有改动
  for (int i = 0; i < block_changes_count; i++) {
    if (block_changes[i].block == 0xFF) continue;
    if (block_changes[i].x == x && block_changes[i].y == y && block_changes[i].z == z) {
#ifdef ALLOW_CHESTS
      // ★ 原来这个位置是箱子，且现在要改成非箱子 → 释放 chest_data
      if (block_changes[i].block == B_chest && block != B_chest) {
        for (int c = 0; c < MAX_CHESTS; c++) {
          if (chest_data[c].used && chest_data[c].x == x &&
              chest_data[c].y == y && chest_data[c].z == z) {
            chest_data[c].used = false;
            break;
          }
        }
      }
#endif
      block_changes[i].block = block;
      return 0;
    }
  }
  // 找空槽
  for (int i = 0; i < MAX_BLOCK_CHANGES; i++) {
    if (block_changes[i].block != 0xFF) continue;
    block_changes[i].x = x;
    block_changes[i].y = y;
    block_changes[i].z = z;
    block_changes[i].block = block;
    if (i >= block_changes_count) block_changes_count = i + 1;
#ifdef ALLOW_CHESTS
    if (block == B_chest) {
      // ★ 在独立数组中初始化箱子
      for (int c = 0; c < MAX_CHESTS; c++) {
        if (chest_data[c].used) continue;
        chest_data[c].used = true;
        chest_data[c].x = x;
        chest_data[c].y = y;
        chest_data[c].z = z;
        memset(chest_data[c].items, 0, sizeof(chest_data[c].items));
        memset(chest_data[c].counts, 0, sizeof(chest_data[c].counts));
        break;
      }
    }
#endif
    return 0;
  }
  return 1;
}

// ============ 方块属性 ============

uint8_t isPassableBlock(uint8_t block) {
  switch (block) {
    case B_air: case B_short_grass: case B_dead_bush: case B_torch:
    case B_oak_sapling: case B_dandelion: case B_poppy: case B_snow:
    case B_moss_carpet: case B_lily_pad:
      return 1;
    default:
      if (block >= B_water && block <= B_water_7) return 1;
      if (block >= B_lava && block <= B_lava_6) return 1;
      return 0;
  }
}

uint8_t isPassableSpawnBlock(uint8_t block) {
  if (isPassableBlock(block)) return 1;
  if (block == B_oak_leaves) return 1;
  return 0;
}

uint8_t isReplaceableBlock(uint8_t block) {
  switch (block) {
    case B_air: case B_short_grass: case B_dead_bush: case B_snow:
    case B_moss_carpet:
      return 1;
    default:
      if (block >= B_water && block <= B_water_7) return 1;
      if (block >= B_lava && block <= B_lava_6) return 1;
      return 0;
  }
}

uint8_t isColumnBlock(uint8_t block) {
  return (block == B_cactus || block == B_sugar_cane);
}

uint8_t isInstantlyMined(PlayerData *player, uint8_t block) {
  (void)player;
  switch (block) {
    case B_short_grass: case B_dead_bush: case B_torch: case B_dandelion:
    case B_poppy: case B_oak_sapling: case B_snow: case B_moss_carpet:
    case B_lily_pad: case B_oak_leaves:
      return 1;
    default: return 0;
  }
}

uint32_t isCompostItem(uint16_t item) {
  switch (item) {
    case I_oak_leaves: case I_oak_sapling: case I_short_grass:
      return 0x40000000;
    default: return 0;
  }
}

uint8_t getItemStackSize(uint16_t item) {
  (void)item;
  return 64;
}

// ============ 挖掘结果 ============

uint16_t getMiningResult(uint16_t held_item, uint8_t block) {
  switch (block) {
    case B_stone: return (held_item >= I_wooden_pickaxe) ? I_cobblestone : 0;
    case B_cobblestone: return (held_item >= I_wooden_pickaxe) ? I_cobblestone : 0;
    case B_dirt: return I_dirt;
    case B_grass_block: return I_dirt;
    case B_snowy_grass_block: return I_dirt;
    case B_sand: return I_sand;
    case B_sandstone: return I_sandstone;
    case B_oak_log: return I_oak_log;
    case B_oak_planks: return I_oak_planks;
    case B_oak_leaves: return (fast_rand() & 15) == 0 ? I_oak_sapling : 0;
    case B_coal_ore: return I_coal;
    case B_iron_ore: return I_raw_iron;
    case B_gold_ore: return I_raw_gold;
    case B_diamond_ore: return I_diamond;
    case B_redstone_ore: return I_redstone;
    case B_copper_ore: return I_copper_ingot;
    case B_crafting_table: return I_crafting_table;
    case B_furnace: return I_furnace;
    case B_cactus: return I_cactus;
    case B_mud: return I_mud;
    case B_ice: return 0;
    case B_snow: return I_snowball;
    case B_snow_block: return I_snowball;
    case B_oak_sapling: return I_oak_sapling;
    case B_torch: return I_torch;
    case B_oak_wood: return I_oak_wood;
    case B_gravel: return (fast_rand() & 15) == 0 ? I_flint : 0;
    default: break;
  }
  if (block < 256 && B_to_I[block] != 0) return B_to_I[block];
  return 0;
}

void bumpToolDurability(PlayerData *player) {
  uint16_t *item = &player->inventory_items[player->hotbar];
  uint8_t *count = &player->inventory_count[player->hotbar];
  (void)item; (void)count;
}

// ============ 玩家管理 ============

void resetPlayerData(PlayerData *player) {
  player->health = 20;
  player->hunger = 20;
  player->saturation = 2500;
  player->x = 8;
  player->z = 8;
  player->y = 80;
  player->flags |= 0x02;
  player->grounded_y = 0;
  for (int i = 0; i < 41; i++) { player->inventory_items[i] = 0; player->inventory_count[i] = 0; }
  for (int i = 0; i < 9; i++) { player->craft_items[i] = 0; player->craft_count[i] = 0; }
  player->flags &= ~0x80;
  player->chest_flags = 0;
  for (int i = 0; i < 27; i++) { player->chest_items[i] = 0; player->chest_count[i] = 0; }

  // 出生装备
  player->inventory_items[0] = I_stone_sword;    player->inventory_count[0] = 1;
  player->inventory_items[1] = I_stone_pickaxe;  player->inventory_count[1] = 1;
  player->inventory_items[2] = I_stone_axe;      player->inventory_count[2] = 1;
  player->inventory_items[3] = I_stone_shovel;   player->inventory_count[3] = 1;
  player->inventory_items[4] = I_stone_hoe;      player->inventory_count[4] = 1;
  player->inventory_items[5] = I_oak_log;        player->inventory_count[5] = 64;
  player->inventory_items[6] = I_cooked_porkchop; player->inventory_count[6] = 64;
  player->inventory_items[7] = I_torch; player->inventory_count[7] = 64;
}

int reservePlayerData(int client_fd, uint8_t *uuid, char *name) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (memcmp(player_data[i].uuid, uuid, 16) == 0) {
      player_data[i].client_fd = client_fd;
      memcpy(player_data[i].name, name, 16);
      player_data[i].flags |= 0x20;
      player_data[i].flagval_16 = 0;
      for (int j = 0; j < VISITED_HISTORY; j++) {
        player_data[i].visited_x[j] = 32767;
        player_data[i].visited_z[j] = 32767;
      }
      return 0;
    }
    uint8_t empty = 1;
    for (uint8_t j = 0; j < 16; j++) { if (player_data[i].uuid[j] != 0) { empty = 0; break; } }
    if (empty) {
      if (player_data_count >= MAX_PLAYERS) return 1;
      player_data[i].client_fd = client_fd;
      player_data[i].flags |= 0x20;
      player_data[i].flagval_16 = 0;
      memcpy(player_data[i].uuid, uuid, 16);
      memcpy(player_data[i].name, name, 16);
      resetPlayerData(&player_data[i]);
      for (int j = 0; j < VISITED_HISTORY; j++) {
        player_data[i].visited_x[j] = 32767;
        player_data[i].visited_z[j] = 32767;
      }
      player_data_count++;
      return 0;
    }
  }
  return 1;
}

int getPlayerData(int client_fd, PlayerData **output) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (player_data[i].client_fd == client_fd) {
      *output = &player_data[i];
      return 0;
    }
  }
  return 1;
}

PlayerData *getPlayerByName(int start_offset, int end_offset, uint8_t *buffer) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (player_data[i].client_fd == -1) continue;
    int j;
    for (j = start_offset; j < end_offset && j < 256 && buffer[j] != ' '; j++) {
      if (player_data[i].name[j - start_offset] != (char)buffer[j]) break;
    }
    if ((j == end_offset || buffer[j] == ' ') && j < 256) return &player_data[i];
  }
  return nullptr;
}

int givePlayerItem(PlayerData *player, uint16_t item, uint8_t count) {
  if (item == 0 || count == 0) return 0;
  uint8_t slot = 255;
  uint8_t stack_size = getItemStackSize(item);

  // ★ 只在 0-35 找堆叠（排除护甲 36-39 和副手 40）
  for (int i = 0; i < 36; i++) {
    if (player->inventory_items[i] == item && player->inventory_count[i] <= stack_size - count) {
      slot = i; break;
    }
  }
  if (slot == 255) {
    for (int i = 0; i < 36; i++) {
      if (player->inventory_count[i] == 0) { slot = i; break; }
    }
  }
  if (slot == 255) return 1;   // 背包满

  player->inventory_items[slot] = item;
  player->inventory_count[slot] += count;

  if (g_sync_slot_cb && player->client_fd >= 0)
    g_sync_slot_cb(player->client_fd, slot, player->inventory_count[slot], player->inventory_items[slot]);

  return 0;
}

// ============ 槽位映射 ============

uint8_t serverSlotToClientSlot(int window_id, uint8_t slot) {
  if (window_id == 0) {
    if (slot < 9) return slot + 36;
    if (slot >= 9 && slot <= 35) return slot;
    if (slot == 40) return 45;
    if (slot >= 36 && slot <= 39) return 44 - slot;
    if (slot >= 41 && slot <= 44) return slot - 40;
  } else if (window_id == 2) {
    // ★ 箱子
    if (slot >= 50 && slot <= 76) return slot - 50;   // 箱子 → 0-26
    if (slot >= 9 && slot <= 35) return slot + 18;    // 背包 → 27-53
    if (slot <= 8) return slot + 54;                  // 快捷栏 → 54-62
  } else if (window_id == 12) {
    if (slot >= 41 && slot <= 49) return slot - 40;
    return serverSlotToClientSlot(0, slot - 1);
  } else if (window_id == 14) {
    if (slot >= 41 && slot <= 43) return slot - 41;
    return serverSlotToClientSlot(0, slot + 6);
  }
  return 255;
}

uint8_t clientSlotToServerSlot(int window_id, uint8_t slot) {
  if (window_id == 0) {
    if (slot >= 36 && slot <= 44) return slot - 36;
    if (slot >= 9 && slot <= 35) return slot;
    if (slot == 45) return 40;
    if (slot >= 5 && slot <= 8) return 44 - slot;
    if (slot == 1) return 41;
    if (slot == 2) return 42;
    if (slot == 3) return 44;
    if (slot == 4) return 45;
  } else if (window_id == 12) {
    if (slot >= 1 && slot <= 9) return 40 + slot;
    if (slot >= 10 && slot <= 45) return clientSlotToServerSlot(0, slot - 1);
  } else if (window_id == 14) {
    if (slot <= 2) return 41 + slot;
    if (slot >= 3 && slot <= 38) return clientSlotToServerSlot(0, slot + 6);
  }
#ifdef ALLOW_CHESTS
  if (window_id == 2) {
    // ★ 与 serverSlotToClientSlot 对称
    if (slot <= 26) return 50 + slot;               // 箱子 → 50-76
    if (slot >= 27 && slot <= 53) return slot - 18; // 背包 → 9-35
    if (slot >= 54 && slot <= 62) return slot - 54; // 快捷栏 → 0-8
  }
#endif
  return 255;
}

// ============ 流体 ============

static uint8_t isReplaceableFluid(uint8_t adjacent, uint8_t level, uint8_t fluid) {
  if (isReplaceableBlock(adjacent) && adjacent != fluid) return 1;
  if (adjacent >= fluid && adjacent < fluid + 8 && (adjacent - fluid) > level + 1) return 1;
  return 0;
}

static void handleFluidMovement(int16_t x, uint8_t y, int16_t z, uint8_t fluid, uint8_t block) {
  uint8_t level = block - fluid;
  uint8_t adjacent[4] = {
    getBlockAt(x + 1, y, z), getBlockAt(x - 1, y, z),
    getBlockAt(x, y, z + 1), getBlockAt(x, y, z - 1)
  };

  if (level != 0) {
    uint8_t connected = 0;
    for (int i = 0; i < 4; i++) { if (adjacent[i] == block - 1) { connected = 1; break; } }
    if (!connected) {
      makeBlockChange(x, y, z, B_air);
      checkFluidUpdate(x + 1, y, z, adjacent[0]);
      checkFluidUpdate(x - 1, y, z, adjacent[1]);
      checkFluidUpdate(x, y, z + 1, adjacent[2]);
      checkFluidUpdate(x, y, z - 1, adjacent[3]);
      return;
    }
  }

  uint8_t block_below = getBlockAt(x, y - 1, z);
  if (isReplaceableBlock(block_below)) {
    makeBlockChange(x, y - 1, z, fluid);
    handleFluidMovement(x, y - 1, z, fluid, fluid);
    return;
  }

  if (level == 3 && fluid == B_lava) return;
  if (level == 7) return;

  if (isReplaceableFluid(adjacent[0], level, fluid)) { makeBlockChange(x + 1, y, z, block + 1); handleFluidMovement(x + 1, y, z, fluid, block + 1); }
  if (isReplaceableFluid(adjacent[1], level, fluid)) { makeBlockChange(x - 1, y, z, block + 1); handleFluidMovement(x - 1, y, z, fluid, block + 1); }
  if (isReplaceableFluid(adjacent[2], level, fluid)) { makeBlockChange(x, y, z + 1, block + 1); handleFluidMovement(x, y, z + 1, fluid, block + 1); }
  if (isReplaceableFluid(adjacent[3], level, fluid)) { makeBlockChange(x, y, z - 1, block + 1); handleFluidMovement(x, y, z - 1, fluid, block + 1); }
}

void checkFluidUpdate(int16_t x, uint8_t y, int16_t z, uint8_t block) {
  uint8_t fluid;
  if (block >= B_water && block < B_water + 8) fluid = B_water;
  else if (block >= B_lava && block < B_lava + 4) fluid = B_lava;
  else return;
  handleFluidMovement(x, y, z, fluid, block);
}

// ============ Mob ============

void spawnMob(uint8_t type, int16_t x, uint8_t y, int16_t z, uint8_t health) {
  for (int i = 0; i < MAX_MOBS; i++) {
    if (mob_data[i].type != 0) continue;
    mob_data[i].type = type;
    mob_data[i].x = x;
    mob_data[i].y = y;
    mob_data[i].z = z;
    mob_data[i].data = health & 31;
    break;
  }
}

// ============ 护甲系统 ============

uint8_t getArmorPoints(uint16_t item) {
    switch (item) {
        case I_leather_helmet: return 1;
        case I_leather_chestplate: return 3;
        case I_leather_leggings: return 2;
        case I_leather_boots: return 1;
        case I_golden_helmet: return 2;
        case I_golden_chestplate: return 5;
        case I_golden_leggings: return 3;
        case I_golden_boots: return 1;
        case I_iron_helmet: return 2;
        case I_iron_chestplate: return 6;
        case I_iron_leggings: return 5;
        case I_iron_boots: return 2;
        case I_diamond_helmet: return 3;
        case I_diamond_chestplate: return 8;
        case I_diamond_leggings: return 6;
        case I_diamond_boots: return 3;
        case I_netherite_helmet: return 3;
        case I_netherite_chestplate: return 8;
        case I_netherite_leggings: return 6;
        case I_netherite_boots: return 3;
        default: return 0;
    }
}

uint8_t getArmorToughness(uint16_t item) {
    switch (item) {
        case I_diamond_helmet: case I_diamond_chestplate:
        case I_diamond_leggings: case I_diamond_boots:
            return 2;
        case I_netherite_helmet: case I_netherite_chestplate:
        case I_netherite_leggings: case I_netherite_boots:
            return 3;
        default: return 0;
    }
}

uint8_t calculateTotalArmor(PlayerData* player) {
    uint8_t total = 0;
    for (int i = 36; i < 40; i++) {
        total += getArmorPoints(player->inventory_items[i]);
    }
    return total;
}

uint8_t applyArmorReduction(PlayerData* player, uint8_t damage) {
    uint8_t armor = calculateTotalArmor(player);
    if (armor == 0) return damage;

    float reduction = armor * 0.04f;
    if (reduction > 0.8f) reduction = 0.8f;

    uint8_t toughness = 0;
    for (int i = 36; i < 40; i++) {
        toughness += getArmorToughness(player->inventory_items[i]);
    }
    if (toughness > 0) {
        float toughness_factor = 1.0f + (toughness / 25.0f);
        reduction = reduction * toughness_factor;
        if (reduction > 0.8f) reduction = 0.8f;
    }

    uint8_t reduced = (uint8_t)(damage * (1.0f - reduction));
    return reduced > 0 ? reduced : 1;
}
