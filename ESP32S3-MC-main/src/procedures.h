#ifndef PROCEDURES_H
#define PROCEDURES_H

#include "game_types.h"
#include <stdint.h>

// 槽位同步回调 (由 mc_server.cpp 注册, 用于 givePlayerItem 实时更新客户端)
extern void (*g_sync_slot_cb)(int fd, int slot, uint8_t count, uint16_t item);
// slot_idx -> 真实 socket fd 的映射表
extern int g_slot_fd_map[MAX_PLAYERS];

// 方块改动查询/写入
uint8_t getBlockChange(int16_t x, uint8_t y, int16_t z);
uint8_t makeBlockChange(int16_t x, uint8_t y, int16_t z, uint8_t block);

// 方块属性
uint8_t isInstantlyMined(PlayerData *player, uint8_t block);
uint8_t isColumnBlock(uint8_t block);
uint8_t isPassableBlock(uint8_t block);
uint8_t isPassableSpawnBlock(uint8_t block);
uint8_t isReplaceableBlock(uint8_t block);
uint32_t isCompostItem(uint16_t item);
uint8_t getItemStackSize(uint16_t item);

// 挖掘和工具
uint16_t getMiningResult(uint16_t held_item, uint8_t block);
void bumpToolDurability(PlayerData *player);

// 玩家管理
void resetPlayerData(PlayerData *player);
int reservePlayerData(int client_fd, uint8_t *uuid, char *name);
int getPlayerData(int client_fd, PlayerData **output);
PlayerData *getPlayerByName(int start_offset, int end_offset, uint8_t *buffer);
int givePlayerItem(PlayerData *player, uint16_t item, uint8_t count);

// 流体
void checkFluidUpdate(int16_t x, uint8_t y, int16_t z, uint8_t block);

// Mob
void spawnMob(uint8_t type, int16_t x, uint8_t y, int16_t z, uint8_t health);

// 槽位映射
uint8_t serverSlotToClientSlot(int window_id, uint8_t slot);
uint8_t clientSlotToServerSlot(int window_id, uint8_t slot);

// ============ 护甲系统 ============
uint8_t getArmorPoints(uint16_t item);
uint8_t getArmorToughness(uint16_t item);
uint8_t calculateTotalArmor(PlayerData* player);
uint8_t applyArmorReduction(PlayerData* player, uint8_t damage);

#endif