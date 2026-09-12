#ifndef GAME_TYPES_H
#define GAME_TYPES_H

#include <stdint.h>

// ============ 编译开关 ============

#define MAX_PLAYERS 5
#define MAX_MOBS 20
#define MOB_DESPAWN_DISTANCE 256
#define GAMEMODE 0
#define VIEW_DISTANCE 2
#define TIME_BETWEEN_TICKS 1000000
#define TICKS_PER_SECOND ((float)1000000 / TIME_BETWEEN_TICKS)
#define INITIAL_WORLD_SEED 0x6A0AEF04
#define INITIAL_RNG_SEED   0x350B10FB
#define CHUNK_SIZE 8
#define TERRAIN_BASE_HEIGHT 60
#define CAVE_BASE_DEPTH 24
#define BIOME_SIZE (CHUNK_SIZE * 8)
#define BIOME_RADIUS (BIOME_SIZE / 2)
#define VISITED_HISTORY 4
#define MAX_BLOCK_CHANGES 5000   // 5000 × 6B = 30KB
#define MAX_CHESTS 8
#define NETWORK_TIMEOUT_US 15000000
#define MAX_RECV_BUF_LEN 256

#define SEND_BRAND
#define BROADCAST_ALL_MOVEMENT
#define SCALE_MOVEMENT_UPDATES_TO_PLAYER_COUNT
#define DO_FLUID_FLOW
#define ALLOW_CHESTS
// #define ENABLE_PLAYER_FLIGHT
#define ENABLE_PICKUP_ANIMATION
#define ENABLE_CACTUS_DAMAGE
// #define DEV_LOG_UNKNOWN_PACKETS
#define DEV_LOG_NETWORK_DIAGNOSTICS
#define DEV_LOG_LENGTH_DISCREPANCY
#define DEV_MINIMAL_PLAY_BOOTSTRAP

// ============ 协议状态 ============

#define STATE_NONE 0
#define STATE_STATUS 1
#define STATE_LOGIN 2
#define STATE_TRANSFER 3
#define STATE_CONFIGURATION 4
#define STATE_PLAY 5

// ============ 伤害类型 ============

#define D_generic 18
#define D_fall 10
#define D_lava 24
#define D_on_fire 31
#define D_cactus 2

// ============ Biome ID ============

#define W_plains 40
#define W_mangrove_swamp 31
#define W_desert 14
#define W_snowy_plains 46
#define W_beach 3

// ============ 数据结构 ============

#pragma pack(push, 1)

struct BlockChange {
  int16_t x;
  int16_t z;
  uint8_t y;
  uint8_t block;
};

struct PlayerData {
  uint8_t uuid[16];
  char name[16];
  int client_fd;
  int16_t x;
  uint8_t y;
  int16_t z;
  int16_t visited_x[VISITED_HISTORY];
  int16_t visited_z[VISITED_HISTORY];
#ifdef SCALE_MOVEMENT_UPDATES_TO_PLAYER_COUNT
  uint16_t packets_since_update;
#endif
  int8_t yaw;
  int8_t pitch;
  uint8_t grounded_y;
  uint8_t health;
  uint8_t hunger;
  uint16_t saturation;
  uint8_t hotbar;
  uint16_t inventory_items[41];
  uint16_t craft_items[9];
  uint8_t inventory_count[41];
  uint8_t craft_count[9];
  uint16_t flagval_16;
  uint8_t flagval_8;
  uint8_t flags;
  int16_t  chest_x;
  uint8_t  chest_y;
  int16_t  chest_z;
  uint16_t chest_items[27];
  uint8_t  chest_count[27];
  uint8_t  chest_flags;
};

struct MobData {
  uint8_t type;
  int16_t x;
  uint8_t y;
  int16_t z;
  uint8_t data;
};

#pragma pack(pop)

struct ChestData {
  bool     used;
  int16_t  x;
  uint8_t  y;
  int16_t  z;
  uint16_t items[27];
  uint8_t  counts[27];
};

union EntityDataValue {
  uint8_t byte_val;
  int pose;
};

struct EntityData {
  uint8_t index;
  int type;
  EntityDataValue value;
};

// ============ 世界生成结构 ============

struct ChunkAnchor {
  int16_t x;
  int16_t z;
  uint32_t hash;
  uint8_t biome;
};

struct ChunkFeature {
  int16_t x;
  uint8_t y;
  int16_t z;
  uint8_t variant;
};

#endif
