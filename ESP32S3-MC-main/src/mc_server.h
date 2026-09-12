#ifndef MC_SERVER_H
#define MC_SERVER_H

#ifdef _WIN32
#include "win_platform.h"
#include "win_network_layer.h"
#else
#include <WiFi.h>
#include "network_layer.h"
#endif

#include "packet_codec.h"
#include "game_types.h"

class MinecraftServer {
 public:
  explicit MinecraftServer(uint16_t port);

  bool begin(const char* ssid, const char* password);
  void poll();

 private:
  // ============ 客户端槽位 ============
  struct ClientSlot {
    int fd;              // socket fd, -1 表示未使用
    uint8_t state;
    bool used;
    bool config_received_info;
    bool config_received_packs;
    uint8_t uuid[16];
    char name[16];
    int player_index;
    // 延迟发块队列
    int8_t chunk_queue_idx;
    int16_t chunk_center_x;
    int16_t chunk_center_z;
    uint32_t chunk_next_send_ms;
    // 移动边缘区块队列
    int8_t edge_queue_count;
    int8_t edge_queue_idx;
    int16_t edge_chunks_x[50];
    int16_t edge_chunks_z[50];
    // 自适应区块发送速率
    uint16_t chunk_interval_ms;   // 当前发块间隔 (ms), 10~500
    uint32_t chunk_send_start_ms; // 上次发块开始时间, 用于测量耗时
    uint8_t  chunk_slow_count;    // 连续超时次数, 用于退避
  };

  static const uint8_t kMaxClients = MAX_PLAYERS;

  // ============ 连接管理 ============
  bool acceptClient_();
  void serviceClient_(uint8_t slot_index);
  void closeClient_(uint8_t slot_index, int cause);

  // ============ 协议状态处理 ============
  bool handleHandshake_(ClientSlot& slot, PacketCodec& codec, int32_t packet_id);
  bool handleStatus_(ClientSlot& slot, PacketCodec& codec, int32_t packet_id, int32_t packet_len);
  bool handleLogin_(ClientSlot& slot, PacketCodec& codec, int32_t packet_id, int32_t packet_len);
  bool handleConfiguration_(ClientSlot& slot, PacketCodec& codec, int32_t packet_id, int32_t packet_len);
  bool handlePlay_(ClientSlot& slot, PacketCodec& codec, int32_t packet_id, int32_t packet_len);

  // ============ 发包: 状态/登录/配置 ============
  bool sendStatusResponse_(PacketCodec& codec);
  bool sendLoginSuccess_(PacketCodec& codec, const uint8_t uuid[16], const char* name);
  bool sendBrand_(PacketCodec& codec);
  bool sendPluginMessage_(PacketCodec& codec, const char* channel, const uint8_t* data, uint32_t data_len);
  bool sendKnownPacks_(PacketCodec& codec);
  bool sendEnabledFeatures_(PacketCodec& codec);
  bool sendRegistries_(PacketCodec& codec);
  bool sendFinishConfiguration_(PacketCodec& codec);

  // ============ 发包: Play ============
  bool sendLoginPlay_(PacketCodec& codec, uint32_t entity_id);
  bool sendSynchronizePlayerPosition_(PacketCodec& codec, double x, double y, double z, float yaw, float pitch);
  bool sendSetDefaultSpawnPosition_(PacketCodec& codec, int64_t x, int64_t y, int64_t z, float yaw, float pitch);
  bool sendStartWaitingForChunks_(PacketCodec& codec);
  bool sendSetCenterChunk_(PacketCodec& codec, int x, int z);
  bool sendChunkDataAndUpdateLight_(PacketCodec& codec, int chunk_x, int chunk_z);
  bool sendKeepAlive_(PacketCodec& codec);
  bool sendSetHealth_(PacketCodec& codec, uint8_t health, uint8_t food, uint16_t saturation);
  bool sendSetHeldItem_(PacketCodec& codec, uint8_t slot);
  bool sendSetContainerSlot_(PacketCodec& codec, int window_id, uint16_t slot, uint8_t count, uint16_t item);
  bool sendBlockUpdate_(PacketCodec& codec, int64_t x, int64_t y, int64_t z, uint8_t block);
  bool sendAcknowledgeBlockChange_(PacketCodec& codec, int sequence);
  bool sendPlayerInfoUpdateAddPlayer_(PacketCodec& codec, PlayerData& player);
  bool sendSpawnEntity_(PacketCodec& codec, int id, uint8_t* uuid, int type, double x, double y, double z, uint8_t yaw, uint8_t pitch);
  bool sendEntityAnimation_(PacketCodec& codec, int id, uint8_t animation);
  bool sendTeleportEntity_(PacketCodec& codec, int id, double x, double y, double z, float yaw, float pitch);
  bool sendSetHeadRotation_(PacketCodec& codec, int id, uint8_t yaw);
  bool sendUpdateEntityRotation_(PacketCodec& codec, int id, uint8_t yaw, uint8_t pitch);
  bool sendDamageEvent_(PacketCodec& codec, int entity_id, int type);
  bool sendRemoveEntity_(PacketCodec& codec, int entity_id);
  bool sendSystemChat_(PacketCodec& codec, const char* message, uint16_t len);
  bool sendEntityEvent_(PacketCodec& codec, int entity_id, uint8_t status);
  bool sendOpenScreen_(PacketCodec& codec, uint8_t window, const char* title, uint16_t length);
  bool sendRespawn_(PacketCodec& codec);
  bool sendPlayerAbilities_(PacketCodec& codec, uint8_t flags);

  // ============ 收包处理 ============
  bool consumeClientInformation_(PacketCodec& codec);
  bool consumePluginMessage_(PacketCodec& codec, int32_t payload_len);
  bool consumeKnownPacks_(PacketCodec& codec);
  bool skipRemainingPacket_(PacketCodec& codec, int32_t packet_len, int32_t packet_id);

  // ============ 游戏逻辑 ============
  void spawnPlayer_(uint8_t slot_index);
  void handlePlayerJoin_(uint8_t slot_index);
  void handlePlayerDisconnect_(uint8_t slot_index);
  void handleServerTick_();
  void handlePlayerAction_(PlayerData* player, int action, int16_t x, int16_t y, int16_t z);
  void handlePlayerUseItem_(PlayerData* player, int16_t x, int16_t y, int16_t z, uint8_t face);
  void hurtEntity_(int entity_id, int attacker_slot, uint8_t damage_type, uint8_t damage);
  void broadcastPlayerMetadata_(PlayerData* player);
  bool handleClickContainer_(uint8_t slot_idx, PacketCodec& codec, int32_t packet_len);
  bool canPlayerEat_(PlayerData* player);
  void doPlayerEat_(PlayerData* player);
  uint8_t getArmorItemSlot_(uint16_t item);
  void broadcastBlockChangesInArea_(int16_t x1, int16_t z1, int16_t x2, int16_t z2);
  void tickMobs_();
  void trySpawnMobNearPlayer_(PlayerData* player);
  void broadcastMobSpawn_(uint8_t type, int16_t x, uint8_t y, int16_t z);

  // ============ 辅助 ============
  PacketCodec codecForSlot_(uint8_t slot_index);
  uint16_t onlineCount_() const;
  int slotIndexForPlayer_(PlayerData* player);
  void processDeferredChunks_(uint8_t slot_index);

  // ============ 成员 ============
  NetworkLayer network_;
  ClientSlot clients_[kMaxClients];
  int64_t last_tick_time_us_;
};

#endif
