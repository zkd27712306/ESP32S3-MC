#include <Arduino.h>
#include "mc_server.h"
#include "game_state.h"
#include "registries.h"
#include "terrain.h"
#include "procedures.h"
#include "crafting.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include "win_platform.h"
#include "win_network_layer.h"
#else
#include <errno.h>
#include <fcntl.h>
#include <lwip/sockets.h>
#include <esp_timer.h>
#endif

namespace {
const char* VERSION_NAME = "26.1.2";
const int PROTOCOL_VERSION = 775;
const int ACTIVE_VIEW_DISTANCE = VIEW_DISTANCE;

#ifdef _WIN32
int64_t get_time_us() { return esp_timer_get_time_win(); }
#else
int64_t get_time_us() { return (int64_t)esp_timer_get_time(); }
#endif
}  // namespace

MinecraftServer::MinecraftServer(uint16_t port) : network_(port), last_tick_time_us_(0) {
  for (uint8_t i = 0; i < kMaxClients; ++i) {
    clients_[i].fd = -1;
    clients_[i].used = false;
    clients_[i].state = STATE_NONE;
    clients_[i].player_index = -1;
    clients_[i].chunk_queue_idx = -1;
    clients_[i].edge_queue_count = 0;
    clients_[i].edge_queue_idx = 0;
    clients_[i].chunk_interval_ms = 80;
    clients_[i].chunk_send_start_ms = 0;
    clients_[i].chunk_slow_count = 0;
    clients_[i].packet_err_count = 0;
  }
}

bool MinecraftServer::begin(const char* ssid, const char* password) {
    world_seed = (uint32_t)splitmix64(world_seed);
    rng_seed = (uint32_t)splitmix64(rng_seed);
    Serial.printf("World seed: %08X, RNG seed: %08X\n", world_seed, rng_seed);

    for (int i = 0; i < MAX_BLOCK_CHANGES; i++) block_changes[i].block = 0xFF;
    for (int i = 0; i < MAX_PLAYERS; i++) player_data[i].client_fd = -1;

    g_sync_slot_cb = [](int fd, int slot, uint8_t count, uint16_t item) {
    if (fd < 0 || fd >= MAX_PLAYERS) return;
    int real_fd = g_slot_fd_map[fd];
    if (real_fd < 0) return;
    PacketCodec pc(real_fd);
    uint16_t client_slot = (uint16_t)serverSlotToClientSlot(0, (uint8_t)slot);
    if (!pc.beginPacket(0x14)) return;
    if (!pc.writeVarInt(0)) return;
    if (!pc.writeVarInt(0)) return;
    if (!pc.writeUint16(client_slot)) return;
    if (!pc.writeVarInt(count)) return;
    if (count > 0) {
      if (!pc.writeVarInt(item)) return;
      if (!pc.writeVarInt(0)) return;
      if (!pc.writeVarInt(0)) return;
    }
    pc.endPacket();
  };

  last_tick_time_us_ = get_time_us();
  return network_.begin(ssid, password);
}

void MinecraftServer::poll() {
    network_.poll();
    if (!network_.connected()) return;

    acceptClient_();

    int64_t now = get_time_us();
    if (now - last_tick_time_us_ > TIME_BETWEEN_TICKS) {
        handleServerTick_();
        last_tick_time_us_ = now;
    }

    for (uint8_t i = 0; i < kMaxClients; ++i) {
        if (!clients_[i].used) continue;
        serviceClient_(i);
    }

#ifndef _WIN32
    static uint32_t last_pkt_err_print_ms = 0;
    uint32_t now_ms_err = millis();
    if (now_ms_err - last_pkt_err_print_ms > 5000) {
        last_pkt_err_print_ms = now_ms_err;
        uint32_t total_err = 0;
        for (uint8_t i = 0; i < kMaxClients; ++i) {
            if (!clients_[i].used) continue;
            total_err += clients_[i].packet_err_count;
        }
        if (total_err > 0) {
            Serial.printf("[PKT_STAT] total packet errors: %u\n", (unsigned)total_err);
        }
    }
#endif

    static uint8_t chunk_send_turn = 0;
    uint32_t now_ms = (uint32_t)(now / 1000);

    if (client_count == 0) return;

    static uint32_t last_chunk_time = 0;
    if (now_ms - last_chunk_time < 50) return;
    last_chunk_time = now_ms;

    auto sendOneChunk = [&](uint8_t i) -> bool {
        ClientSlot& cs = clients_[i];
        if (!cs.used || cs.fd < 0) return false;
        if ((int32_t)(now_ms - cs.chunk_next_send_ms) < 0) return false;

        bool sent = false;
        cs.chunk_send_start_ms = now_ms;

        if (cs.chunk_queue_idx >= 0) {
            processDeferredChunks_(i);
            sent = true;
        } else if (cs.edge_queue_idx < cs.edge_queue_count) {
            PacketCodec pc(cs.fd);
            int8_t ei = cs.edge_queue_idx;
            sendChunkDataAndUpdateLight_(pc, cs.edge_chunks_x[ei], cs.edge_chunks_z[ei]);
            cs.edge_queue_idx++;
            sent = true;
        }

        if (sent) {
            uint32_t elapsed = (uint32_t)(millis() - cs.chunk_send_start_ms);
            if (elapsed < cs.chunk_interval_ms / 2) {
                cs.chunk_interval_ms = (uint16_t)(cs.chunk_interval_ms * 85 / 100);
                if (cs.chunk_interval_ms < 10) cs.chunk_interval_ms = 10;
                if (cs.chunk_slow_count > 0) cs.chunk_slow_count--;
            } else if (elapsed > cs.chunk_interval_ms) {
                cs.chunk_slow_count++;
                if (cs.chunk_slow_count >= 3) {
                    cs.chunk_interval_ms = 300;
                    cs.chunk_slow_count = 0;
                } else {
                    cs.chunk_interval_ms = (uint16_t)(cs.chunk_interval_ms * 150 / 100);
                    if (cs.chunk_interval_ms > 500) cs.chunk_interval_ms = 500;
                }
            }
            cs.chunk_next_send_ms = millis() + cs.chunk_interval_ms;
        }
        return sent;
    };

#ifdef _WIN32
    for (uint8_t attempt = 0; attempt < kMaxClients; attempt++) {
        uint8_t i = (chunk_send_turn + attempt) % kMaxClients;
        if (sendOneChunk(i)) { chunk_send_turn = (i + 1) % kMaxClients; break; }
    }
#else
    uint32_t free_mem = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    if (free_mem < 50000) {
        Serial.printf("[MEM] Emergency! free: %u, stopping chunk sends\n", free_mem);
        for (uint8_t i = 0; i < kMaxClients; i++) {
            if (clients_[i].used) {
                clients_[i].edge_queue_count = 0;
                clients_[i].edge_queue_idx = 0;
                clients_[i].chunk_queue_idx = -1;
            }
        }
        heap_caps_check_integrity_all(true);
        vTaskDelay(50);
        return;
    }

    if (free_mem > 90000) {
        for (uint8_t attempt = 0; attempt < kMaxClients; attempt++) {
            uint8_t i = (chunk_send_turn + attempt) % kMaxClients;
            if (sendOneChunk(i)) { chunk_send_turn = (i + 1) % kMaxClients; break; }
        }
    } else if (free_mem > 60000) {
        for (uint8_t i = 0; i < kMaxClients; i++) {
            if (clients_[i].chunk_interval_ms < 200) clients_[i].chunk_interval_ms = 200;
        }
        for (uint8_t attempt = 0; attempt < kMaxClients; attempt++) {
            uint8_t i = (chunk_send_turn + attempt) % kMaxClients;
            if (sendOneChunk(i)) { chunk_send_turn = (i + 1) % kMaxClients; break; }
        }
    } else {
        vTaskDelay(25);
    }
#endif
}

#ifndef _WIN32
static WiFiClient kept_clients[MAX_PLAYERS];
#endif

bool MinecraftServer::acceptClient_() {
#ifdef _WIN32
  SOCKET new_sock = network_.acceptClient();
  if (new_sock == INVALID_SOCKET) return false;
  int new_fd = (int)new_sock;
#else
  WiFiClient client = network_.accept();
  if (!client) return false;
  int new_fd = client.fd();
  if (new_fd < 0) { client.stop(); return false; }
  int flags = fcntl(new_fd, F_GETFL, 0);
  if (flags >= 0) fcntl(new_fd, F_SETFL, flags | O_NONBLOCK);
#endif

  for (uint8_t i = 0; i < kMaxClients; ++i) {
    if (clients_[i].used) continue;
    clients_[i].fd = new_fd;
    clients_[i].state = STATE_NONE;
    clients_[i].used = true;
    clients_[i].config_received_info = false;
    clients_[i].config_received_packs = false;
    clients_[i].player_index = -1;
    clients_[i].chunk_queue_idx = -1;
    clients_[i].edge_queue_count = 0;
    clients_[i].edge_queue_idx = 0;
    clients_[i].chunk_interval_ms = 80;
    clients_[i].chunk_send_start_ms = 0;
    clients_[i].chunk_slow_count = 0;
    clients_[i].packet_err_count = 0;
    memset(clients_[i].uuid, 0, 16);
    memset(clients_[i].name, 0, 16);
    g_slot_fd_map[i] = new_fd;
    client_count++;
#ifndef _WIN32
    kept_clients[i] = client;
#endif
    return true;
  }
#ifdef _WIN32
  closesocket(new_sock);
#else
  client.stop();
#endif
  return false;
}

void MinecraftServer::serviceClient_(uint8_t slot_index) {
  ClientSlot& slot = clients_[slot_index];
  if (slot.fd < 0) { closeClient_(slot_index, 1); return; }

  fd_set fds;
  struct timeval tv = {0, 0};
  FD_ZERO(&fds);
  FD_SET(slot.fd, &fds);
  if (select(slot.fd + 1, &fds, NULL, NULL, &tv) <= 0) return;

  uint8_t peek_buf[2];
  int peek_n = recv(slot.fd, peek_buf, 2, MSG_PEEK);
  if (peek_n <= 0) {
    if (peek_n == 0) { closeClient_(slot_index, 1); return; }
    if (errno == EAGAIN || errno == EWOULDBLOCK) return;
    closeClient_(slot_index, 1);
    return;
  }

  if (peek_buf[0] == 0 && peek_buf[1] == 0) {
    uint8_t dummy[256];
    while (recv(slot.fd, (char*)dummy, sizeof(dummy), MSG_DONTWAIT) > 0) {}
    return;
  }

  PacketCodec codec(slot.fd);
  codec.resetReadCount();

  int32_t packet_len = 0;
  if (!codec.readVarInt(packet_len)) {
    uint8_t dummy[256];
    while (recv(slot.fd, (char*)dummy, sizeof(dummy), MSG_DONTWAIT) > 0) {}
    return;
  }

  if (packet_len <= 0 || packet_len > 65536) {
    slot.packet_err_count++;
    Serial.printf("[PKT_ERR] slot=%u bad packet_len=%d, draining\n",
                  (unsigned)slot_index, (int)packet_len);
    uint8_t dummy[256];
    while (recv(slot.fd, (char*)dummy, sizeof(dummy), MSG_DONTWAIT) > 0) {}
    return;
  }

  int32_t packet_id = 0;
  if (!codec.readVarInt(packet_id)) {
    uint8_t dummy[256];
    while (recv(slot.fd, (char*)dummy, sizeof(dummy), MSG_DONTWAIT) > 0) {}
    return;
  }

  int32_t payload_len = packet_len - codec.sizeVarInt((uint32_t)packet_id);
  if (payload_len < 0 || payload_len > 65536) {
    slot.packet_err_count++;
    Serial.printf("[PKT_ERR] slot=%u payload_len=%d pkt_len=%d id=%d\n",
                  (unsigned)slot_index, (int)payload_len, (int)packet_len, (int)packet_id);
    uint8_t dummy[256];
    while (recv(slot.fd, (char*)dummy, sizeof(dummy), MSG_DONTWAIT) > 0) {}
    return;
  }

  if (packet_id == 0x1D) {
    if (payload_len > 0) codec.skipBytes((size_t)payload_len);
    return;
  }

  bool ok = false;
  switch (slot.state) {
    case STATE_NONE: ok = handleHandshake_(slot, codec, packet_id); break;
    case STATE_STATUS: ok = handleStatus_(slot, codec, packet_id, payload_len); break;
    case STATE_LOGIN: ok = handleLogin_(slot, codec, packet_id, payload_len); break;
    case STATE_CONFIGURATION: ok = handleConfiguration_(slot, codec, packet_id, payload_len); break;
    case STATE_PLAY: ok = handlePlay_(slot, codec, packet_id, payload_len); break;
    default: ok = false; break;
  }

  size_t consumed = codec.readCount()
                  - codec.sizeVarInt((uint32_t)packet_len)
                  - codec.sizeVarInt((uint32_t)packet_id);
  if (consumed < (size_t)payload_len) {
    codec.skipBytes((size_t)payload_len - consumed);
  }

  if (!ok) slot.packet_err_count++;
}

void MinecraftServer::closeClient_(uint8_t slot_index, int cause) {
    ClientSlot& slot = clients_[slot_index];
    if (!slot.used) return;

    if (slot.player_index >= 0) handlePlayerDisconnect_(slot_index);

#ifdef _WIN32
    if (slot.fd >= 0) { closesocket((SOCKET)slot.fd); slot.fd = -1; }
#else
    if (kept_clients[slot_index]) {
        kept_clients[slot_index].stop();
        kept_clients[slot_index] = WiFiClient();
    }
    slot.fd = -1;
#endif

    slot.state = STATE_NONE;
    slot.used = false;
    slot.player_index = -1;
    g_slot_fd_map[slot_index] = -1;

    if (client_count > 0) client_count--;

  Serial.printf("Client %d disconnected, cause: %d\n", slot_index, cause);
}

bool MinecraftServer::handleHandshake_(ClientSlot& slot, PacketCodec& codec, int32_t packet_id) {
  if (packet_id != 0x00) return false;
  int32_t protocol_version = 0;
  char server_address[256];
  uint16_t server_port = 0;
  int32_t next_state = 0;
  if (!codec.readVarInt(protocol_version)) return false;
  if (!codec.readString(server_address, sizeof(server_address))) return false;
  if (!codec.readUint16(server_port)) return false;
  if (!codec.readVarInt(next_state)) return false;
  slot.state = (uint8_t)next_state;
  return true;
}

bool MinecraftServer::handleStatus_(ClientSlot& slot, PacketCodec& codec, int32_t packet_id, int32_t packet_len) {
  if (packet_id == 0x00) return sendStatusResponse_(codec);
  if (packet_id == 0x01) {
    uint8_t payload[8];
    if (!codec.readExact(payload, 8)) return false;
    PacketCodec pc(slot.fd);
    if (!pc.beginPacket(0x01)) return false;
    if (!pc.writeExact(payload, 8)) return false;
    pc.endPacket();
    closeClient_((uint8_t)(&slot - clients_), 8);
    return true;
  }
  return codec.skipBytes((size_t)packet_len);
}

bool MinecraftServer::sendStatusResponse_(PacketCodec& codec) {
  char json[512];
  int len = snprintf(json, sizeof(json),
    "{\"version\":{\"name\":\"%s\",\"protocol\":%d},"
    "\"players\":{\"max\":%d,\"online\":%d},"
    "\"description\":{\"text\":\"ESP32MC Server - Type !help for commands\"}}",
    VERSION_NAME, PROTOCOL_VERSION, MAX_PLAYERS, onlineCount_());
  uint32_t json_len = (uint32_t)len;
  if (!codec.beginPacket(0x00)) return false;
  if (!codec.writeVarInt(json_len)) return false;
  if (!codec.writeExact((const uint8_t*)json, json_len)) return false;
  return codec.endPacket();
}

bool MinecraftServer::handleLogin_(ClientSlot& slot, PacketCodec& codec, int32_t packet_id, int32_t packet_len) {
  if (packet_id == 0x00) {
    if (!codec.readString(slot.name, sizeof(slot.name))) return false;
    if (!codec.readExact(slot.uuid, 16)) return false;
    uint8_t slot_idx = (uint8_t)(&slot - clients_);
    if (reservePlayerData(slot_idx, slot.uuid, slot.name)) return false;
    PlayerData* p;
    if (getPlayerData(slot_idx, &p) == 0) slot.player_index = (int)(p - player_data);
    return sendLoginSuccess_(codec, slot.uuid, slot.name);
  }
  if (packet_id == 0x03) {
    slot.state = STATE_CONFIGURATION;
    if (!sendBrand_(codec)) return false;
    if (!sendEnabledFeatures_(codec)) return false;
    return sendKnownPacks_(codec);
  }
  return codec.skipBytes((size_t)packet_len);
}

bool MinecraftServer::sendLoginSuccess_(PacketCodec& codec, const uint8_t uuid[16], const char* name) {
  uint32_t name_len = (uint32_t)strlen(name);
  if (!codec.beginPacket(0x02)) return false;
  if (!codec.writeExact(uuid, 16)) return false;
  if (!codec.writeVarInt(name_len)) return false;
  if (!codec.writeExact((const uint8_t*)name, name_len)) return false;
  if (!codec.writeVarInt(0)) return false;
  return codec.endPacket();
}

bool MinecraftServer::handleConfiguration_(ClientSlot& slot, PacketCodec& codec, int32_t packet_id, int32_t packet_len) {
  if (packet_id == 0x00) { slot.config_received_info = true; return consumeClientInformation_(codec); }
  if (packet_id == 0x02) return consumePluginMessage_(codec, packet_len);
  if (packet_id == 0x07) {
    if (!consumeKnownPacks_(codec)) return false;
    slot.config_received_packs = true;
    if (!sendRegistries_(codec)) return false;
    return sendFinishConfiguration_(codec);
  }
  if (packet_id == 0x03) {
    uint8_t slot_idx = (uint8_t)(&slot - clients_);
    slot.state = STATE_PLAY;
    if (!sendLoginPlay_(codec, slot_idx)) return false;
    spawnPlayer_(slot_idx);
    return true;
  }
  return codec.skipBytes((size_t)packet_len);
}

bool MinecraftServer::handlePlay_(ClientSlot& slot, PacketCodec& codec, int32_t packet_id, int32_t packet_len) {
    uint8_t slot_idx = (uint8_t)(&slot - clients_);
    PlayerData* player = nullptr;
    if (slot.player_index >= 0) player = &player_data[slot.player_index];
  switch (packet_id) {
    case 0x00:
    case 0x0F:
        return codec.skipBytes((size_t)packet_len);
    case 0x1C: {
      uint64_t id;
      if (!codec.readUint64(id)) return false;
      return true;
    }
    case 0x0B:
    case 0x0D:
        return codec.skipBytes((size_t)packet_len);

    case 0x1E:
    case 0x1F:
    case 0x20:
    case 0x21: {
      if (!player) return codec.skipBytes((size_t)packet_len);
      double x = 0, y = 0, z = 0;
      float yaw = 0, pitch = 0;

      if (packet_id == 0x1E || packet_id == 0x1F) {
        double dx, dy, dz;
        if (!codec.readDouble(dx) || !codec.readDouble(dy) || !codec.readDouble(dz)) return false;
        x = dx; y = dy; z = dz;
      }
      if (packet_id == 0x1F || packet_id == 0x20) {
        float fy, fp;
        if (!codec.readFloat(fy) || !codec.readFloat(fp)) return false;
        yaw = fy; pitch = fp;
      }
      uint8_t flags_byte;
      if (!codec.readByte(flags_byte)) return false;
      uint8_t on_ground = flags_byte & 0x01;

      if (on_ground && packet_id != 0x20) {
        int16_t damage = player->grounded_y - player->y - 3;
        if (damage > 0 && (GAMEMODE == 0 || GAMEMODE == 2)) {
          uint8_t feet = getBlockAt(player->x, player->y, player->z);
          uint8_t waist = getBlockAt(player->x, player->y + 1, player->z);
          bool in_water = (feet >= B_water && feet <= B_water_7) ||
                          (waist >= B_water && waist <= B_water_7);
          if (!in_water) hurtEntity_(slot_idx, -1, D_fall, (uint8_t)damage);
        }
        player->grounded_y = player->y;
      }

      if (packet_id == 0x21) return true;

      if (packet_id != 0x1E) {
        player->yaw = ((int16_t)(yaw + 540) % 360 - 180) * 127 / 180;
        player->pitch = (int8_t)(pitch / 90.0f * 127.0f);
      }

      if (packet_id == 0x20) {
        for (uint8_t i = 0; i < kMaxClients; i++) {
          if (!clients_[i].used || clients_[i].state != STATE_PLAY || i == slot_idx) continue;
          if (clients_[i].player_index < 0) continue;
          PacketCodec oc(clients_[i].fd);
          sendUpdateEntityRotation_(oc, slot_idx, player->yaw, player->pitch);
          sendSetHeadRotation_(oc, slot_idx, player->yaw);
        }
        return true;
      }

      if (x < -30000000 || x > 30000000 ||
          z < -30000000 || z > 30000000 ||
          y < -64 || y > 320) return true;

      int16_t cx = (int16_t)x, cy = (int16_t)y, cz = (int16_t)z;
      if (!isPassableBlock(getBlockAt(cx, cy, cz)) || !isPassableBlock(getBlockAt(cx, cy + 1, cz)))
        return true;

      int16_t prev_chunk_x = div_floor(player->x, 16);
      int16_t prev_chunk_z = div_floor(player->z, 16);
      player->x = cx; player->y = (uint8_t)cy; player->z = cz;

      if (player->saturation == 0) {
        if (player->hunger > 0) player->hunger--;
        player->saturation = 200;
        PacketCodec hpc(slot.fd);
        sendSetHealth_(hpc, player->health, player->hunger, player->saturation);
      } else if (player->flags & 0x08) {
        player->saturation -= 1;
      }

      for (uint8_t i = 0; i < kMaxClients; i++) {
        if (!clients_[i].used || clients_[i].state != STATE_PLAY || i == slot_idx) continue;
        if (clients_[i].player_index < 0) continue;
        PacketCodec oc(clients_[i].fd);
        sendTeleportEntity_(oc, slot_idx, x, y, z, yaw, pitch);
        sendSetHeadRotation_(oc, slot_idx, player->yaw);
      }

      int16_t new_chunk_x = div_floor(cx, 16), new_chunk_z = div_floor(cz, 16);
      if (new_chunk_x != prev_chunk_x || new_chunk_z != prev_chunk_z) {
        PacketCodec pc(slot.fd);
        sendStartWaitingForChunks_(pc);
        sendSetCenterChunk_(pc, new_chunk_x, new_chunk_z);
        int16_t dx = new_chunk_x - prev_chunk_x;
        int16_t dz = new_chunk_z - prev_chunk_z;

        int remaining = slot.edge_queue_count - slot.edge_queue_idx;
        if (remaining > 0 && slot.edge_queue_idx > 0) {
          for (int j = 0; j < remaining; j++) {
            slot.edge_chunks_x[j] = slot.edge_chunks_x[slot.edge_queue_idx + j];
            slot.edge_chunks_z[j] = slot.edge_chunks_z[slot.edge_queue_idx + j];
          }
        } else if (remaining <= 0) {
          remaining = 0;
        }
        slot.edge_queue_count = (int8_t)remaining;
        slot.edge_queue_idx = 0;

        auto enqueue = [&](int16_t ex, int16_t ez) {
          if (slot.edge_queue_count >= 50) return;
          for (int j = 0; j < slot.edge_queue_count; j++)
            if (slot.edge_chunks_x[j] == ex && slot.edge_chunks_z[j] == ez) return;
          slot.edge_chunks_x[slot.edge_queue_count] = ex;
          slot.edge_chunks_z[slot.edge_queue_count] = ez;
          slot.edge_queue_count++;
        };

        if (dx != 0) {
          int16_t edge_x = new_chunk_x + (dx > 0 ? ACTIVE_VIEW_DISTANCE : -ACTIVE_VIEW_DISTANCE);
          for (int j = -ACTIVE_VIEW_DISTANCE; j <= ACTIVE_VIEW_DISTANCE; j++)
            enqueue(edge_x, new_chunk_z + j);
        }
        if (dz != 0) {
          int16_t edge_z = new_chunk_z + (dz > 0 ? ACTIVE_VIEW_DISTANCE : -ACTIVE_VIEW_DISTANCE);
          for (int j = -ACTIVE_VIEW_DISTANCE; j <= ACTIVE_VIEW_DISTANCE; j++)
            enqueue(new_chunk_x + j, edge_z);
        }
        trySpawnMobNearPlayer_(player);
      }
      return true;
    }

    case 0x29: {
      if (!player) return codec.skipBytes((size_t)packet_len);
      uint8_t action; if (!codec.readByte(action)) return false;
      uint64_t pos_raw; if (!codec.readUint64(pos_raw)) return false;
      int32_t bx = (int32_t)(pos_raw >> 38);
      int32_t by = (int32_t)((pos_raw << 52) >> 52);
      int32_t bz = (int32_t)((pos_raw << 26) >> 38);
      uint8_t face; if (!codec.readByte(face)) return false;
      int32_t sequence; if (!codec.readVarInt(sequence)) return false;
      if (bx < -30000000 || bx > 30000000 ||
          bz < -30000000 || bz > 30000000 ||
          by < -64 || by > 320) return true;
      PacketCodec pc(slot.fd);
      sendAcknowledgeBlockChange_(pc, sequence);
      handlePlayerAction_(player, action, (int16_t)bx, (int16_t)by, (int16_t)bz);
      return true;
    }

    case 0x42: {
      if (!player) return codec.skipBytes((size_t)packet_len);
      uint8_t hand; if (!codec.readByte(hand)) return false;
      uint64_t pos_raw; if (!codec.readUint64(pos_raw)) return false;
      int32_t bx = (int32_t)(pos_raw >> 38);
      int32_t by = (int32_t)((pos_raw << 52) >> 52);
      int32_t bz = (int32_t)((pos_raw << 26) >> 38);
      uint8_t face; if (!codec.readByte(face)) return false;
      codec.skipBytes(12 + 2);
      int32_t sequence; if (!codec.readVarInt(sequence)) return false;
      if (bx < -30000000 || bx > 30000000 ||
          bz < -30000000 || bz > 30000000 ||
          by < -64 || by > 320) return true;
      PacketCodec pc(slot.fd);
      sendAcknowledgeBlockChange_(pc, sequence);

      uint16_t held = player->inventory_items[player->hotbar];
      if (held == I_bow && player->inventory_count[player->hotbar] > 0) {
        uint8_t arrow_slot = 255;
        for (uint8_t i = 0; i < 41; i++) {
          if (player->inventory_items[i] == I_arrow && player->inventory_count[i] > 0) {
            arrow_slot = i; break;
          }
        }
        if (arrow_slot != 255) {
          player->inventory_count[arrow_slot]--;
          if (player->inventory_count[arrow_slot] == 0) player->inventory_items[arrow_slot] = 0;
          sendSetContainerSlot_(pc, 0, serverSlotToClientSlot(0, arrow_slot),
              player->inventory_count[arrow_slot], player->inventory_items[arrow_slot]);

          int target_entity = -1;
          float angle = player->yaw * 180.0f / 127.0f;
          float rad = angle * 3.14159f / 180.0f;
          int dx_dir = (int)(sin(rad) * 2);
          int dz_dir = (int)(cos(rad) * 2);

          for (int d = 1; d < 30; d++) {
            int16_t tx = player->x + d * dx_dir;
            int16_t tz = player->z + d * dz_dir;
            for (int i = 0; i < MAX_MOBS; i++) {
              if (mob_data[i].type == 0) continue;
              if ((mob_data[i].data & 31) == 0) continue;
              int16_t dx = mob_data[i].x - tx;
              int16_t dz = mob_data[i].z - tz;
              if (dx*dx + dz*dz < 3) { target_entity = -2 - i; break; }
            }
            if (target_entity != -1) break;
          }

          if (target_entity != -1) {
            hurtEntity_(target_entity, slot_idx, D_arrow, 8);
            for (uint8_t i = 0; i < kMaxClients; i++) {
              if (!clients_[i].used || clients_[i].state != STATE_PLAY) continue;
              PacketCodec oc(clients_[i].fd);
              sendEntityEvent_(oc, target_entity, 2);
            }
          }
          return true;
        }
      }

      if (held == I_water_bucket) {
        if (face == 255) return true;
        static uint32_t last_bucket_time[MAX_PLAYERS] = {0};
        uint32_t now = millis();
        if (now - last_bucket_time[slot_idx] < 500) return true;
        last_bucket_time[slot_idx] = now;

        int16_t px = bx, py = by, pz = bz;
        switch (face) {
          case 0: py -= 1; break; case 1: py += 1; break;
          case 2: pz -= 1; break; case 3: pz += 1; break;
          case 4: px -= 1; break; case 5: px += 1; break;
        }
        uint8_t target = getBlockAt(px, py, pz);
        if (isReplaceableBlock(target)) {
          makeBlockChange(px, (uint8_t)py, pz, B_water);
          for (uint8_t i = 0; i < kMaxClients; i++) {
            if (!clients_[i].used || clients_[i].state != STATE_PLAY) continue;
            PacketCodec oc(clients_[i].fd);
            sendBlockUpdate_(oc, px, py, pz, B_water);
          }
          sendSetContainerSlot_(pc, 0, serverSlotToClientSlot(0, player->hotbar), 1, I_water_bucket);
          sendSetHeldItem_(pc, player->hotbar);
        }
        return true;
      }

      handlePlayerUseItem_(player, (int16_t)bx, (int16_t)by, (int16_t)bz, face);
      return true;
    }

    case 0x35: {
      if (!player) return codec.skipBytes((size_t)packet_len);
      uint16_t held_slot; if (!codec.readUint16(held_slot)) return false;
      if (held_slot < 9) player->hotbar = (uint8_t)held_slot;
      return true;
    }

    case 0x09: {
      if (!player) return codec.skipBytes((size_t)packet_len);
      char msg[225]; if (!codec.readString(msg, sizeof(msg))) return false;
      codec.skipBytes(8 + 8);
      uint8_t has_sig; codec.readByte(has_sig);
      if (has_sig) codec.skipBytes(256);
      int32_t msg_count; codec.readVarInt(msg_count);
      codec.skipBytes(4);

      PacketCodec pc(slot.fd);

      if (msg[0] == '!') {
        if (strncmp(msg, "!help", 5) == 0) {
          sendSystemChat_(pc, "=== ESP32MC Commands ===", strlen("=== ESP32MC Commands ==="));
          sendSystemChat_(pc, "!help - Show this help", strlen("!help - Show this help"));
          sendSystemChat_(pc, "!msg <p> <msg> - Whisper", strlen("!msg <p> <msg> - Whisper"));
          sendSystemChat_(pc, "!summon <mob> [n] - Spawn", strlen("!summon <mob> [n] - Spawn"));
          sendSystemChat_(pc, "!give <p> <item> [count]", strlen("!give <p> <item> [count]"));
          sendSystemChat_(pc, "!overworld - Back to spawn", strlen("!overworld - Back to spawn"));
          sendSystemChat_(pc, "Mobs: chicken cow pig sheep zombie skeleton spider creeper", strlen("Mobs: chicken cow pig sheep zombie skeleton spider creeper"));
        }
        else if (strncmp(msg, "!overworld", 10) == 0) {
          uint8_t ground_y = getHeightAt(8, 8);
          if (ground_y < 5) ground_y = 80;
          player->x = 8;
          player->y = ground_y + 2;
          player->z = 8;
          player->grounded_y = ground_y + 1;
          PacketCodec pc2(slot.fd);
          sendSynchronizePlayerPosition_(pc2, 8.5, (double)(ground_y + 1.5), 8.5, 0, 0);
          sendSetDefaultSpawnPosition_(pc2, 8, ground_y + 2, 8, 0, 0);
          sendSystemChat_(pc2, "Welcome back to Overworld!", strlen("Welcome back to Overworld!"));
          return true;
        }
        else if (strncmp(msg, "!msg ", 5) == 0) {
          char* target_start = msg + 5;
          while (*target_start == ' ') target_start++;
          char* space = strchr(target_start, ' ');
          if (!space) { sendSystemChat_(pc, "Usage: !msg <player> <message>", strlen("Usage: !msg <player> <message>")); return true; }
          *space = '\0';
          char* text = space + 1;
          PlayerData* target = nullptr;
          for (int i = 0; i < MAX_PLAYERS; i++) {
            if (player_data[i].client_fd == -1) continue;
            if (strcmp(player_data[i].name, target_start) == 0) { target = &player_data[i]; break; }
          }
          if (!target) { sendSystemChat_(pc, "Player not found", strlen("Player not found")); return true; }
          int ti = slotIndexForPlayer_(target);
          if (ti >= 0) {
            char whisper[256];
            int wl = snprintf(whisper, sizeof(whisper), "\xC2\xA7" "7" "\xC2\xA7" "o%s whispers to you: %s", player->name, text);
            PacketCodec tc(clients_[ti].fd);
            sendSystemChat_(tc, whisper, (uint16_t)wl);
          }
          char reply[256];
          int rl = snprintf(reply, sizeof(reply), "\xC2\xA7" "7" "\xC2\xA7" "oYou whisper to %s: %s", target_start, text);
          sendSystemChat_(pc, reply, (uint16_t)rl);
        }
        else if (strncmp(msg, "!summon ", 8) == 0) {
          char* args = msg + 8;
          while (*args == ' ') args++;
          char mob_name[32] = {};
          int count = 1;
          char* space = strchr(args, ' ');
          if (space) {
            int nlen = (int)(space - args);
            if (nlen > 31) nlen = 31;
            memcpy(mob_name, args, nlen);
            count = atoi(space + 1);
            if (count < 1) count = 1;
            if (count > 10) count = 10;
          } else {
            strncpy(mob_name, args, 31);
          }
          uint8_t type = 0; uint8_t hp = 10;
          if      (strcmp(mob_name, "chicken") == 0) { type = 26;  hp = 4;  }
          else if (strcmp(mob_name, "cow")     == 0) { type = 30;  hp = 10; }
          else if (strcmp(mob_name, "pig")     == 0) { type = 100; hp = 10; }
          else if (strcmp(mob_name, "sheep")   == 0) { type = 111; hp = 8;  }
          else if (strcmp(mob_name, "zombie")  == 0) { type = 150; hp = 20; }
          else if (strcmp(mob_name, "skeleton")== 0) { type = 115; hp = 20; }
          else if (strcmp(mob_name, "spider")  == 0) { type = 124; hp = 16; }
          else if (strcmp(mob_name, "creeper") == 0) { type = 32;  hp = 20; }
          else if (mob_name[0] >= '0' && mob_name[0] <= '9') { type = (uint8_t)atoi(mob_name); hp = 20; }
          if (type == 0) {
            sendSystemChat_(pc, "Unknown mob. Try: chicken cow pig sheep zombie skeleton spider creeper", strlen("Unknown mob. Try: chicken cow pig sheep zombie skeleton spider creeper"));
          } else {
            int spawned = 0;
            for (int n = 0; n < count; n++) {
              uint32_t r = fast_rand();
              int16_t sx = player->x + (int16_t)((r & 7) - 3);
              int16_t sz = player->z + (int16_t)(((r >> 4) & 7) - 3);
              uint8_t sy = player->y;
              for (int t = 0; t < 10; t++) {
                if (!isPassableBlock(getBlockAt(sx, sy - 1, sz)) &&
                     isPassableBlock(getBlockAt(sx, sy,     sz)) &&
                     isPassableBlock(getBlockAt(sx, sy + 1, sz))) break;
                sy++;
              }
              spawnMob(type, sx, sy, sz, hp);
              broadcastMobSpawn_(type, sx, sy, sz);
              spawned++;
            }
            char out[64];
            int ol = snprintf(out, sizeof(out), "Spawned %d %s", spawned, mob_name);
            sendSystemChat_(pc, out, (uint16_t)ol);
          }
        }
        else if (strncmp(msg, "!give ", 6) == 0) {
          char* args = msg + 6;
          while (*args == ' ') args++;
          char target_name[32] = {};
          char item_name[32] = {};
          int count = 1;

          char* space1 = strchr(args, ' ');
          if (!space1) { sendSystemChat_(pc, "Usage: !give <player> <item> [count]", strlen("Usage: !give <player> <item> [count]")); return true; }
          int tn = (int)(space1 - args);
          if (tn > 31) tn = 31;
          memcpy(target_name, args, tn);
          target_name[tn] = '\0';

          char* rest = space1 + 1;
          while (*rest == ' ') rest++;
          char* space2 = strchr(rest, ' ');
          if (space2) {
            int in = (int)(space2 - rest);
            if (in > 31) in = 31;
            memcpy(item_name, rest, in);
            item_name[in] = '\0';
            count = atoi(space2 + 1);
            if (count < 1) count = 1;
            if (count > 64) count = 64;
          } else {
            strncpy(item_name, rest, 31);
            item_name[31] = '\0';
          }

          PlayerData* target = nullptr;
          for (int i = 0; i < MAX_PLAYERS; i++) {
            if (player_data[i].client_fd == -1) continue;
            if (strcmp(player_data[i].name, target_name) == 0) { target = &player_data[i]; break; }
          }
          if (!target) { sendSystemChat_(pc, "Player not found", strlen("Player not found")); return true; }

          uint16_t item_id = 0;
          if      (strcmp(item_name, "stone") == 0)         item_id = I_stone;
          else if (strcmp(item_name, "dirt") == 0)          item_id = I_dirt;
          else if (strcmp(item_name, "cobblestone") == 0)   item_id = I_cobblestone;
          else if (strcmp(item_name, "oak_log") == 0)       item_id = I_oak_log;
          else if (strcmp(item_name, "oak_planks") == 0)    item_id = I_oak_planks;
          else if (strcmp(item_name, "iron_ingot") == 0)    item_id = I_iron_ingot;
          else if (strcmp(item_name, "gold_ingot") == 0)    item_id = I_gold_ingot;
          else if (strcmp(item_name, "diamond") == 0)       item_id = I_diamond;
          else if (strcmp(item_name, "coal") == 0)          item_id = I_coal;
          else if (strcmp(item_name, "redstone") == 0)      item_id = I_redstone;
          else if (strcmp(item_name, "copper_ingot") == 0)  item_id = I_copper_ingot;
          else if (strcmp(item_name, "stick") == 0)         item_id = I_stick;
          else if (strcmp(item_name, "torch") == 0)         item_id = I_torch;
          else if (strcmp(item_name, "apple") == 0)         item_id = I_apple;
          else if (strcmp(item_name, "bread") == 0)         item_id = I_bread;
          else if (strcmp(item_name, "cooked_beef") == 0)   item_id = I_cooked_beef;
          else if (strcmp(item_name, "cooked_porkchop")==0) item_id = I_cooked_porkchop;
          else if (strcmp(item_name, "cooked_chicken")==0)  item_id = I_cooked_chicken;
          else if (strcmp(item_name, "diamond_block")==0)   item_id = I_diamond_block;
          else if (strcmp(item_name, "iron_block")==0)      item_id = I_iron_block;
          else if (strcmp(item_name, "gold_block")==0)      item_id = I_gold_block;
          else if (strcmp(item_name, "coal_block")==0)      item_id = I_coal_block;
          else if (strcmp(item_name, "redstone_block")==0)  item_id = I_redstone_block;
          else if (strcmp(item_name, "copper_block")==0)    item_id = I_copper_block;

          if (item_id == 0) {
            sendSystemChat_(pc, "Unknown item. Try: stone, dirt, cobblestone, oak_log, iron_ingot, diamond, etc.", strlen("Unknown item. Try: stone, dirt, cobblestone, oak_log, iron_ingot, diamond, etc."));
            return true;
          }

          givePlayerItem(target, item_id, (uint8_t)count);
          char result[64];
          int rl = snprintf(result, sizeof(result), "Gave %d %s to %s", count, item_name, target_name);
          sendSystemChat_(pc, result, (uint16_t)rl);
        }
        else {
          sendSystemChat_(pc, "Unknown command. Try !help", strlen("Unknown command. Try !help"));
        }
      } else {
        char buf[256];
        int blen = snprintf(buf, sizeof(buf), "<%s> %s", player->name, msg);
        for (uint8_t i = 0; i < kMaxClients; i++) {
          if (!clients_[i].used || clients_[i].state != STATE_PLAY) continue;
          PacketCodec oc(clients_[i].fd);
          sendSystemChat_(oc, buf, (uint16_t)blen);
        }
      }
      return true;
    }

    case 0x2C: {
      if (!player) return codec.skipBytes((size_t)packet_len);
      handlePlayerJoin_(slot_idx);
      return true;
    }

    case 0x3F: {
      if (!player) return codec.skipBytes((size_t)packet_len);
      int32_t hand; codec.readVarInt(hand);
      uint8_t animation = (hand == 0) ? 0 : 2;
      for (uint8_t i = 0; i < kMaxClients; i++) {
        if (!clients_[i].used || clients_[i].state != STATE_PLAY || i == slot_idx) continue;
        PacketCodec oc(clients_[i].fd);
        sendEntityAnimation_(oc, slot_idx, animation);
      }
      return true;
    }

    case 0x01: {
      if (!player) return codec.skipBytes((size_t)packet_len);
      int32_t entity_id; if (!codec.readVarInt(entity_id)) return false;
      hurtEntity_(entity_id, slot_idx, D_generic, 1);
      return true;
    }

    case 0x1A: {
      if (!player) return codec.skipBytes((size_t)packet_len);
      int32_t entity_id; if (!codec.readVarInt(entity_id)) return false;
      uint8_t type; if (!codec.readByte(type)) return false;
      if (type == 2) codec.skipBytes(12);
      if (type != 1) codec.skipBytes(1);
      codec.skipBytes(1);
      if (type == 1) hurtEntity_(entity_id, slot_idx, D_generic, 1);
      return true;
    }

    case 0x12:
      return handleClickContainer_(slot_idx, codec, packet_len);

    case 0x13: {
      if (!player) return codec.skipBytes((size_t)packet_len);
      int32_t window_id; codec.readVarInt(window_id);
      for (uint8_t i = 0; i < 9; i++) {
        if (window_id != 2) {
          uint16_t craft_item = player->craft_items[i];
          if (craft_item != I_water_bucket && craft_item != I_bucket &&
              craft_item != I_lava_bucket && craft_item != I_milk_bucket) {
            givePlayerItem(player, craft_item, player->craft_count[i]);
          }
        }
        player->craft_items[i] = 0;
        player->craft_count[i] = 0;
        player->flags &= ~0x80;
      }
      givePlayerItem(player, player->flagval_16, player->flagval_8);
      player->flagval_16 = 0;
      player->flagval_8 = 0;
      PacketCodec pc(slot.fd);
      sendSetContainerSlot_(pc, 0, 0, 0, 0);
      for (uint8_t i = 1; i <= 4; i++)
        sendSetContainerSlot_(pc, 0, i, 0, 0);
      for (uint8_t i = 0; i < 41; i++)
        sendSetContainerSlot_(pc, 0, serverSlotToClientSlot(0, i), player->inventory_count[i], player->inventory_items[i]);
      return true;
    }

    case 0x0C: {
      if (!player) return codec.skipBytes((size_t)packet_len);
      uint8_t action_id; if (!codec.readByte(action_id)) return false;
      if (action_id == 0) {
        PacketCodec pc(slot.fd);
        sendRespawn_(pc);
        resetPlayerData(player);
        spawnPlayer_(slot_idx);
      }
      return true;
    }

    case 0x2A: {
      if (!player) return codec.skipBytes((size_t)packet_len);
      int32_t eid; codec.readVarInt(eid);
      uint8_t action; codec.readByte(action);
      int32_t jump_boost; codec.readVarInt(jump_boost);
      if (action == 0) player->flags |= 0x04;
      else if (action == 1) player->flags &= ~0x04;
      else if (action == 3) player->flags |= 0x08;
      else if (action == 4) player->flags &= ~0x08;
      broadcastPlayerMetadata_(player);
      return true;
    }

    case 0x2B: {
      if (!player) return codec.skipBytes((size_t)packet_len);
      uint8_t flags_in; codec.readByte(flags_in);
      if (flags_in & 0x20) player->flags |= 0x04;
      else player->flags &= ~0x04;
      broadcastPlayerMetadata_(player);
      return true;
    }

    case 0x43: {
      if (!player) return codec.skipBytes((size_t)packet_len);
      uint8_t hand; codec.readByte(hand);
      int32_t sequence; codec.readVarInt(sequence);
      codec.skipBytes(8);
      if (canPlayerEat_(player)) {
        player->flagval_16 = 0;
        player->flags |= 0x10;
      }
      return true;
    }

    default:
      return codec.skipBytes((size_t)packet_len);
  }
}

// ============================================================
// 游戏逻辑
// ============================================================

void MinecraftServer::spawnPlayer_(uint8_t slot_idx) {
  ClientSlot& slot = clients_[slot_idx];
  if (slot.player_index < 0) return;
  PlayerData* player = &player_data[slot.player_index];
  PacketCodec codec(slot.fd);

  float spawn_x = 8.5f, spawn_y = 80.0f, spawn_z = 8.5f;
  if (player->flags & 0x02) {
    spawn_y = getHeightAt(8, 8) + 1;
    player->y = (uint8_t)spawn_y;
    player->flags &= ~0x02;
  } else {
    spawn_x = (float)player->x + 0.5f;
    spawn_y = player->y;
    spawn_z = (float)player->z + 0.5f;
  }

  sendSynchronizePlayerPosition_(codec, spawn_x, spawn_y, spawn_z, 0, 0);
  sendSetDefaultSpawnPosition_(codec, 8, 80, 8, 0, 0);
  sendStartWaitingForChunks_(codec);

  int16_t cx = div_floor(player->x, 16), cz = div_floor(player->z, 16);
  sendSetCenterChunk_(codec, cx, cz);
  sendChunkDataAndUpdateLight_(codec, cx, cz);

  slot.chunk_queue_idx = 0;
  slot.chunk_center_x = cx;
  slot.chunk_center_z = cz;
  slot.chunk_next_send_ms = millis() + slot.chunk_interval_ms;
}

void MinecraftServer::handlePlayerJoin_(uint8_t slot_idx) {
  ClientSlot& slot = clients_[slot_idx];
  if (slot.player_index < 0) return;
  PlayerData* player = &player_data[slot.player_index];
  player->flags &= ~0x20;

  for (int attempt = 0; attempt < 8; attempt++) trySpawnMobNearPlayer_(player);

  char buf[64];
  int blen = snprintf(buf, sizeof(buf), "%s joined the game", player->name);
  for (uint8_t i = 0; i < kMaxClients; i++) {
    if (!clients_[i].used || clients_[i].state != STATE_PLAY) continue;
    PacketCodec oc(clients_[i].fd);
    sendSystemChat_(oc, buf, (uint16_t)blen);
    sendPlayerInfoUpdateAddPlayer_(oc, *player);
    if (i != slot_idx) {
      sendSpawnEntity_(oc, slot_idx, player->uuid, 155,
        player->x + 0.5, player->y, player->z + 0.5, player->yaw, player->pitch);
    }
  }

  PacketCodec pc(slot.fd);
  for (uint8_t i = 0; i < kMaxClients; i++) {
    if (i == slot_idx || !clients_[i].used || clients_[i].state != STATE_PLAY) continue;
    if (clients_[i].player_index < 0) continue;
    PlayerData& other = player_data[clients_[i].player_index];
    sendPlayerInfoUpdateAddPlayer_(pc, other);
    sendSpawnEntity_(pc, i, other.uuid, 155, other.x + 0.5, other.y, other.z + 0.5, other.yaw, other.pitch);
  }

  for (int i = 0; i < MAX_MOBS; i++) {
    if (mob_data[i].type == 0) continue;
    if ((mob_data[i].data & 31) == 0) continue;
    uint8_t uuid[16];
    uint32_t r = fast_rand();
    memcpy(uuid, &r, 4);
    memcpy(uuid + 4, &i, 4);
    memset(uuid + 8, 0, 8);
    sendSpawnEntity_(pc, -2 - i, uuid, mob_data[i].type,
      mob_data[i].x + 0.5, mob_data[i].y, mob_data[i].z + 0.5, 0, 0);
  }
}

void MinecraftServer::handlePlayerDisconnect_(uint8_t slot_idx) {
  ClientSlot& slot = clients_[slot_idx];
  if (slot.player_index < 0) return;
  PlayerData* player = &player_data[slot.player_index];
  player->client_fd = -1;

  char buf[64];
  int blen = snprintf(buf, sizeof(buf), "%s left the game", player->name);
  for (uint8_t i = 0; i < kMaxClients; i++) {
    if (!clients_[i].used || clients_[i].state != STATE_PLAY || i == slot_idx) continue;
    PacketCodec oc(clients_[i].fd);
    sendSystemChat_(oc, buf, (uint16_t)blen);
    sendRemoveEntity_(oc, slot_idx);
  }
}

void MinecraftServer::handleServerTick_() {
  world_time = (world_time + TIME_BETWEEN_TICKS / 50000) % 24000;
  server_ticks++;

  if (server_ticks % 30 == 0) {
    for (uint8_t i = 0; i < kMaxClients; i++) {
      if (!clients_[i].used || clients_[i].state != STATE_PLAY) continue;
      if (clients_[i].player_index < 0) continue;

      PlayerData* player = &player_data[clients_[i].player_index];
      int16_t cx = div_floor(player->x, 16);
      int16_t cz = div_floor(player->z, 16);

      ClientSlot& slot = clients_[i];
      int new_count = 0;
      for (int j = 0; j < slot.edge_queue_count; j++) {
        int16_t dx = slot.edge_chunks_x[j] - cx;
        int16_t dz = slot.edge_chunks_z[j] - cz;
        if (dx*dx + dz*dz <= 9) {
          slot.edge_chunks_x[new_count] = slot.edge_chunks_x[j];
          slot.edge_chunks_z[new_count] = slot.edge_chunks_z[j];
          new_count++;
        }
      }
      slot.edge_queue_count = new_count;
      if (slot.edge_queue_idx > slot.edge_queue_count) slot.edge_queue_idx = slot.edge_queue_count;
      break;
    }
  }

  for (uint8_t i = 0; i < kMaxClients; i++) {
    if (!clients_[i].used || clients_[i].state != STATE_PLAY) continue;
    if (clients_[i].player_index < 0) continue;
    PlayerData* player = &player_data[clients_[i].player_index];
    if (player->client_fd == -1) continue;
    PacketCodec pc(clients_[i].fd);

    if (player->flags & 0x01) {
      if (player->flagval_8 >= (uint8_t)(0.6f * TICKS_PER_SECOND)) {
        player->flags &= ~0x01; player->flagval_8 = 0;
      } else player->flagval_8++;
    }

    if (player->flags & 0x10) {
      if (player->flagval_16 >= (uint16_t)(1.6f * TICKS_PER_SECOND)) {
        doPlayerEat_(player);
        player->flags &= ~0x10;
        player->flagval_16 = 0;
      } else player->flagval_16++;
    }

#ifndef BROADCAST_ALL_MOVEMENT
    player->flags &= ~0x40;
#endif

    if (server_ticks % (uint32_t)TICKS_PER_SECOND != 0) continue;
    sendKeepAlive_(pc);

    uint8_t block = getBlockAt(player->x, player->y, player->z);
    if (block >= B_lava && block < B_lava + 4) hurtEntity_(i, -1, D_lava, 8);

    uint8_t head_block = getBlockAt(player->x, player->y + 1, player->z);
    bool in_water = (head_block >= B_water && head_block <= B_water_7);
    uint8_t air_ticks = (player->flagval_16 >> 8) & 0xFF;
    if (in_water) {
      if (air_ticks < 15) {
        air_ticks++;
        player->flagval_16 = (player->flagval_16 & 0x00FF) | ((uint16_t)air_ticks << 8);
      }
      if (air_ticks >= 15) hurtEntity_(i, -1, D_drown, 2);
      int16_t air = (int16_t)(300 - air_ticks * 20);
      if (air < 0) air = 0;
      if (!pc.beginPacket(0x63)) continue;
      if (!pc.writeVarInt(i)) continue;
      if (!pc.writeByte(1)) continue;
      if (!pc.writeVarInt(1)) continue;
      if (!pc.writeVarInt((uint32_t)air)) continue;
      if (!pc.writeByte(0xFF)) continue;
      pc.endPacket();
    } else if (air_ticks > 0) {
      player->flagval_16 = player->flagval_16 & 0x00FF;
      if (!pc.beginPacket(0x63)) continue;
      if (!pc.writeVarInt(i)) continue;
      if (!pc.writeByte(1)) continue;
      if (!pc.writeVarInt(1)) continue;
      if (!pc.writeVarInt(300)) continue;
      if (!pc.writeByte(0xFF)) continue;
      pc.endPacket();
    }

#ifdef ENABLE_CACTUS_DAMAGE
    if (block == B_cactus ||
        getBlockAt(player->x + 1, player->y, player->z) == B_cactus ||
        getBlockAt(player->x - 1, player->y, player->z) == B_cactus ||
        getBlockAt(player->x, player->y, player->z + 1) == B_cactus ||
        getBlockAt(player->x, player->y, player->z - 1) == B_cactus)
      hurtEntity_(i, -1, D_cactus, 4);
#endif

    if (player->health < 20 && player->health > 0 && player->hunger >= 18) {
      if (player->saturation >= 600) { player->saturation -= 600; player->health++; }
      else { player->hunger--; player->health++; }
      sendSetHealth_(pc, player->health, player->hunger, player->saturation);
    }
  }

  if (server_ticks % (uint32_t)TICKS_PER_SECOND == 0) tickMobs_();
  if (rng_seed == 0) rng_seed = world_seed;
}

void MinecraftServer::handlePlayerAction_(PlayerData* player, int action, int16_t x, int16_t y, int16_t z) {
  if (action == 3 || action == 4) {
    int pi = slotIndexForPlayer_(player);
    if (pi >= 0) {
      PacketCodec pc(clients_[pi].fd);
      sendSetContainerSlot_(pc, 0, serverSlotToClientSlot(0, player->hotbar),
        player->inventory_count[player->hotbar], player->inventory_items[player->hotbar]);
    }
    return;
  }

  if (action == 5) { player->flagval_16 = 0; player->flags &= ~0x10; return; }
  if (action != 0 && action != 2) return;

  if (action == 0 && GAMEMODE == 1) {
    makeBlockChange(x, (uint8_t)y, z, 0);
    for (uint8_t i = 0; i < kMaxClients; i++) {
      if (!clients_[i].used || clients_[i].state != STATE_PLAY) continue;
      PacketCodec oc(clients_[i].fd);
      sendBlockUpdate_(oc, x, y, z, B_air);
    }
    return;
  }

  uint8_t block = getBlockAt(x, y, z);
  if (action == 0 && !isInstantlyMined(player, block)) return;
  if (makeBlockChange(x, (uint8_t)y, z, 0)) return;

  uint16_t held_item = player->inventory_items[player->hotbar];
  uint16_t item = getMiningResult(held_item, block);
  bumpToolDurability(player);
  if (item) givePlayerItem(player, item, 1);

  for (uint8_t i = 0; i < kMaxClients; i++) {
    if (!clients_[i].used || clients_[i].state != STATE_PLAY) continue;
    PacketCodec oc(clients_[i].fd);
    sendBlockUpdate_(oc, x, y, z, B_air);
  }

  int pi = slotIndexForPlayer_(player);
  if (pi >= 0 && item) {
    PacketCodec pc(clients_[pi].fd);
    for (uint8_t i = 0; i < 41; i++) {
      if (player->inventory_items[i] == item) {
        sendSetContainerSlot_(pc, 0, serverSlotToClientSlot(0, i), player->inventory_count[i], player->inventory_items[i]);
        break;
      }
    }
  }

#ifdef DO_FLUID_FLOW
  uint8_t block_above = getBlockAt(x, y + 1, z);
  checkFluidUpdate(x, (uint8_t)(y + 1), z, block_above);
  checkFluidUpdate(x - 1, (uint8_t)y, z, getBlockAt(x - 1, y, z));
  checkFluidUpdate(x + 1, (uint8_t)y, z, getBlockAt(x + 1, y, z));
  checkFluidUpdate(x, (uint8_t)y, z - 1, getBlockAt(x, y, z - 1));
  checkFluidUpdate(x, (uint8_t)y, z + 1, getBlockAt(x, y, z + 1));
#else
  uint8_t block_above = getBlockAt(x, y + 1, z);
#endif

  uint8_t y_offset = 1;
  while (isColumnBlock(block_above)) {
    makeBlockChange(x, (uint8_t)(y + y_offset), z, 0);
    uint16_t col_item = getMiningResult(0, block_above);
    if (col_item) givePlayerItem(player, col_item, 1);
    for (uint8_t i = 0; i < kMaxClients; i++) {
      if (!clients_[i].used || clients_[i].state != STATE_PLAY) continue;
      PacketCodec oc(clients_[i].fd);
      sendBlockUpdate_(oc, x, y + y_offset, z, B_air);
    }
    y_offset++;
    block_above = getBlockAt(x, y + y_offset, z);
  }
}

void MinecraftServer::handlePlayerUseItem_(PlayerData* player, int16_t x, int16_t y, int16_t z, uint8_t face) {
    uint8_t target = (face == 255) ? 0 : getBlockAt(x, y, z);
    uint8_t *count = &player->inventory_count[player->hotbar];
    uint16_t *item = &player->inventory_items[player->hotbar];
    int pi = slotIndexForPlayer_(player);
    if (pi < 0) return;
    PacketCodec pc(clients_[pi].fd);

if (*item == I_torch) {
    if (face == 255) return;
    static uint32_t last_torch_time[MAX_PLAYERS] = {0};
    uint32_t now = millis();
    if (pi >= 0 && pi < MAX_PLAYERS) {
        if (now - last_torch_time[pi] < 300) return;
        last_torch_time[pi] = now;
    }
    int16_t px = x, py = y, pz = z;
    switch (face) {
        case 0: py -= 1; break; case 1: py += 1; break;
        case 2: pz -= 1; break; case 3: pz += 1; break;
        case 4: px -= 1; break; case 5: px += 1; break;
    }
    uint8_t target_block = getBlockAt(px, py, pz);
    if (target_block >= B_water && target_block <= B_water_7) return;
    if (target_block >= B_lava && target_block <= B_lava_6) return;
    if (isReplaceableBlock(target_block)) {
        uint8_t attach_block = getBlockAt(x, y, z);
        if (attach_block >= B_water && attach_block <= B_water_7) return;
        if (attach_block >= B_lava && attach_block <= B_lava_6) return;
        if (!isPassableBlock(attach_block)) {
            makeBlockChange(px, (uint8_t)py, pz, B_torch);
            *count -= 1;
            if (*count == 0) *item = 0;
            for (uint8_t i = 0; i < kMaxClients; i++) {
                if (!clients_[i].used || clients_[i].state != STATE_PLAY) continue;
                PacketCodec oc(clients_[i].fd);
                sendBlockUpdate_(oc, px, py, pz, B_torch);
            }
            sendSetContainerSlot_(pc, 0, serverSlotToClientSlot(0, player->hotbar), *count, *item);
            sendSetHeldItem_(pc, player->hotbar);
            return;
        }
    }
    return;
}

    if (!(player->flags & 0x04) && face != 255) {
        if (target == B_crafting_table) {
            sendOpenScreen_(pc, 12, "Crafting", 8);
            return;
        } else if (target == B_furnace) {
            sendOpenScreen_(pc, 14, "Furnace", 7);
            return;
        } else if (target == B_composter && *count > 0) {
            uint32_t compost_chance = isCompostItem(*item);
            if (compost_chance != 0) {
                if ((*count -= 1) == 0) *item = 0;
                sendSetContainerSlot_(pc, 0, serverSlotToClientSlot(0, player->hotbar), *count, *item);
                if (fast_rand() < compost_chance) givePlayerItem(player, I_bone_meal, 1);
                return;
            }
        }
#ifdef ALLOW_CHESTS
        else if (target == B_chest) {
            uint8_t *storage_ptr = nullptr;
            for (int i = 0; i < block_changes_count; i++) {
                if (block_changes[i].block != B_chest) continue;
                if (block_changes[i].x != x || block_changes[i].y != (uint8_t)y || block_changes[i].z != z) continue;
                storage_ptr = (uint8_t *)(&block_changes[i + 1]);
                break;
            }
            if (storage_ptr == nullptr) return;
            memcpy(player->craft_items, &storage_ptr, sizeof(storage_ptr));
            player->flags |= 0x80;
            sendOpenScreen_(pc, 2, "Chest", 5);
            for (int i = 0; i < 27; i++) {
                uint16_t ci; uint8_t cc;
                memcpy(&ci, storage_ptr + i * 3, 2);
                memcpy(&cc, storage_ptr + i * 3 + 2, 1);
                sendSetContainerSlot_(pc, 2, i, cc, ci);
            }
            return;
        }
#endif
    }

    if (*count == 0) return;

    if (*item == I_bone_meal && face != 255) {
        uint8_t target_below = getBlockAt(x, y - 1, z);
        if (target == B_oak_sapling) {
            if ((*count -= 1) == 0) *item = 0;
            sendSetContainerSlot_(pc, 0, serverSlotToClientSlot(0, player->hotbar), *count, *item);
            if ((target_below == B_dirt || target_below == B_grass_block || target_below == B_snowy_grass_block || target_below == B_mud) &&
                (fast_rand() & 3) == 0) {
                placeTreeStructure(x, (uint8_t)y, z);
                broadcastBlockChangesInArea_(x - 3, z - 3, x + 3, z + 3);
            }
            return;
        }
    }

    if (canPlayerEat_(player)) {
        player->flagval_16 = 0;
        player->flags |= 0x10;
        return;
    }

    if (face == 255) return;
    uint8_t block = I_to_B(*item);
    if (block == 0) return;

    switch (face) {
        case 0: y -= 1; break; case 1: y += 1; break;
        case 2: z -= 1; break; case 3: z += 1; break;
        case 4: x -= 1; break; case 5: x += 1; break;
        default: break;
    }

    if (!isPassableBlock(block) && x == player->x && (y == player->y || y == player->y + 1) && z == player->z) return;

    if (isReplaceableBlock(getBlockAt(x, y, z)) && (!isColumnBlock(block) || getBlockAt(x, y - 1, z) != B_air)) {
        if (makeBlockChange(x, (uint8_t)y, z, block)) return;
        *count -= 1;
        if (*count == 0) *item = 0;

#ifdef DO_FLUID_FLOW
        checkFluidUpdate(x, y + 1, z, getBlockAt(x, y + 1, z));
        checkFluidUpdate(x - 1, (uint8_t)y, z, getBlockAt(x - 1, y, z));
        checkFluidUpdate(x + 1, (uint8_t)y, z, getBlockAt(x + 1, y, z));
        checkFluidUpdate(x, (uint8_t)y, z - 1, getBlockAt(x, y, z - 1));
        checkFluidUpdate(x, (uint8_t)y, z + 1, getBlockAt(x, y, z + 1));
#endif

        for (uint8_t i = 0; i < kMaxClients; i++) {
            if (!clients_[i].used || clients_[i].state != STATE_PLAY) continue;
            PacketCodec oc(clients_[i].fd);
            sendBlockUpdate_(oc, x, y, z, block);
        }
    }

    sendSetContainerSlot_(pc, 0, serverSlotToClientSlot(0, player->hotbar), *count, *item);
    sendSetHeldItem_(pc, player->hotbar);
}

void MinecraftServer::hurtEntity_(int entity_id, int attacker_slot, uint8_t damage_type, uint8_t damage) {
    if (entity_id <= -2) {
        int mob_idx = -2 - entity_id;
        if (mob_idx < 0 || mob_idx >= MAX_MOBS) return;
        if (mob_data[mob_idx].type == 0) return;

        if (attacker_slot >= 0 && attacker_slot < kMaxClients && clients_[attacker_slot].player_index >= 0) {
            PlayerData* attacker = &player_data[clients_[attacker_slot].player_index];
            if (attacker->flags & 0x01) return;
            uint16_t held = attacker->inventory_items[attacker->hotbar];
            if (held == I_wooden_sword || held == I_golden_sword) damage *= 4;
            else if (held == I_stone_sword) damage *= 5;
            else if (held == I_iron_sword) damage *= 6;
            else if (held == I_diamond_sword) damage *= 7;
            else if (held == I_netherite_sword) damage *= 8;
            else if (held == I_mace) damage *= 10;
            attacker->flags |= 0x01; attacker->flagval_8 = 0;
        }

        uint8_t hp = mob_data[mob_idx].data & 31;
        if (hp <= damage) {
            mob_data[mob_idx].data &= ~31;
            mob_data[mob_idx].y = 0;

            PlayerData* killer = nullptr;
            if (attacker_slot >= 0 && attacker_slot < kMaxClients && clients_[attacker_slot].player_index >= 0)
                killer = &player_data[clients_[attacker_slot].player_index];
            if (killer) {
                uint32_t r = fast_rand();
                switch (mob_data[mob_idx].type) {
                    case 26: givePlayerItem(killer, I_chicken, 1); break;
                    case 30: givePlayerItem(killer, I_beef, 1 + (r % 3)); break;
                    case 100: givePlayerItem(killer, I_porkchop, 1 + (r % 3)); break;
                    case 111: givePlayerItem(killer, I_mutton, 1 + (r & 1)); break;
                    case 150: givePlayerItem(killer, I_rotten_flesh, r % 3); break;
                    case 115: givePlayerItem(killer, I_bone, 1 + (r & 1));
                              if ((r >> 2) & 1) givePlayerItem(killer, I_arrow, 1 + (r & 1)); break;
                    case 124: if ((r >> 2) & 1) givePlayerItem(killer, I_string, 1 + (r & 1)); break;
                    default: break;
                }
            }
        } else {
            mob_data[mob_idx].data = (mob_data[mob_idx].data & ~31) | (hp - damage);
        }

        for (uint8_t i = 0; i < kMaxClients; i++) {
            if (!clients_[i].used || clients_[i].state != STATE_PLAY) continue;
            PacketCodec oc(clients_[i].fd);
            sendDamageEvent_(oc, entity_id, damage_type);
            if ((mob_data[mob_idx].data & 31) == 0) sendEntityEvent_(oc, entity_id, 3);
        }
        return;
    }

if (entity_id < 0 || entity_id >= kMaxClients) return;
if (!clients_[entity_id].used || clients_[entity_id].player_index < 0) return;
PlayerData* player = &player_data[clients_[entity_id].player_index];
if (player->health == 0) return;

damage = applyArmorReduction(player, damage);

if (attacker_slot >= 0 && attacker_slot < kMaxClients && clients_[attacker_slot].player_index >= 0) {
    PlayerData* attacker = &player_data[clients_[attacker_slot].player_index];
    if (attacker->flags & 0x01) return;
    uint16_t held = attacker->inventory_items[attacker->hotbar];
    if (held == I_wooden_sword || held == I_golden_sword) damage *= 4;
    else if (held == I_stone_sword) damage *= 5;
    else if (held == I_iron_sword) damage *= 6;
    else if (held == I_diamond_sword) damage *= 7;
    else if (held == I_netherite_sword) damage *= 8;
    else if (held == I_mace) damage *= 10;
    attacker->flags |= 0x01; attacker->flagval_8 = 0;
}

if (damage == 0) return;

if (player->health <= damage) player->health = 0;
else player->health -= damage;

PacketCodec pc(clients_[entity_id].fd);
sendSetHealth_(pc, player->health, player->hunger, player->saturation);

for (uint8_t i = 0; i < kMaxClients; i++) {
    if (!clients_[i].used || clients_[i].state != STATE_PLAY) continue;
    PacketCodec oc(clients_[i].fd);
    sendDamageEvent_(oc, entity_id, damage_type);
    if (player->health == 0) sendEntityEvent_(oc, entity_id, 3);
}
}

void MinecraftServer::broadcastPlayerMetadata_(PlayerData* player) {
  uint8_t sneaking = (player->flags & 0x04) != 0;
  uint8_t sprinting = (player->flags & 0x08) != 0;
  uint8_t entity_bit_mask = 0;
  if (sneaking) entity_bit_mask |= 0x02;
  if (sprinting) entity_bit_mask |= 0x08;

  int pi = slotIndexForPlayer_(player);
  if (pi < 0) return;

  for (uint8_t i = 0; i < kMaxClients; i++) {
    if (!clients_[i].used || clients_[i].state != STATE_PLAY || (int)i == pi) continue;
    PacketCodec oc(clients_[i].fd);
    if (!oc.beginPacket(0x63)) continue;
    if (!oc.writeVarInt((uint32_t)pi)) continue;
    if (!oc.writeByte(0)) continue;
    if (!oc.writeVarInt(0)) continue;
    if (!oc.writeByte(entity_bit_mask)) continue;
    if (!oc.writeByte(0xFF)) continue;
    oc.endPacket();
  }
}

bool MinecraftServer::handleClickContainer_(uint8_t slot_idx, PacketCodec& codec, int32_t packet_len) {
    ClientSlot& slot = clients_[slot_idx];
    PlayerData* player = (slot.player_index >= 0) ? &player_data[slot.player_index] : nullptr;
    if (!player) return codec.skipBytes((size_t)packet_len);

    int32_t window_id, state_id, mode_i, changes_count;
    uint16_t clicked_slot_raw;
    uint8_t button;
    if (!codec.readVarInt(window_id)) return false;
    if (!codec.readVarInt(state_id)) return false;
    if (!codec.readUint16(clicked_slot_raw)) return false;
    if (!codec.readByte(button)) return false;
    if (!codec.readVarInt(mode_i)) return false;
    if (!codec.readVarInt(changes_count)) return false;
    if (changes_count < 0 || changes_count > 64) {
        Serial.printf("[PKT_ERR] click changes_count=%d\n", (int)changes_count);
        return false;
    }
    int16_t clicked_slot = (int16_t)clicked_slot_raw;
    uint8_t mode = (uint8_t)mode_i;

    PacketCodec pc(slot.fd);
    uint8_t apply_changes = 1;

    if ((window_id == 0 || window_id == 12) && clicked_slot == 0 && mode == 0) {
        uint8_t out_count; uint16_t out_item;
        getCraftingOutput(player, &out_count, &out_item);
        if (out_item != 0 && out_count > 0) {
            for (int i = 0; i < 9; i++) {
                if (player->craft_items[i] != 0) {
                    player->craft_count[i]--;
                    if (player->craft_count[i] == 0) player->craft_items[i] = 0;
                }
            }
            if (player->flagval_16 == 0) {
                player->flagval_16 = out_item;
                player->flagval_8 = out_count;
            } else if (player->flagval_16 == out_item) {
                player->flagval_8 += out_count;
            }
            uint8_t new_count; uint16_t new_item;
            getCraftingOutput(player, &new_count, &new_item);
            sendSetContainerSlot_(pc, window_id, 0, new_count, new_item);
            for (int i = 0; i < 9; i++) {
                uint16_t cs = serverSlotToClientSlot(window_id, 41 + i);
                sendSetContainerSlot_(pc, window_id, cs, player->craft_count[i], player->craft_items[i]);
            }
        }
        apply_changes = 0;
    }

    if (mode == 4 && clicked_slot != -999) {
        uint8_t s = clientSlotToServerSlot(window_id, (uint8_t)clicked_slot);
        sendSetContainerSlot_(pc, window_id, clicked_slot_raw, player->inventory_count[s], player->inventory_items[s]);
        apply_changes = 0;
    } else if (mode == 0 && clicked_slot == -999) {
        if (button == 0) {
            givePlayerItem(player, player->flagval_16, player->flagval_8);
            player->flagval_16 = 0; player->flagval_8 = 0;
        } else {
            givePlayerItem(player, player->flagval_16, 1);
            player->flagval_8 -= 1;
            if (player->flagval_8 == 0) player->flagval_16 = 0;
        }
        apply_changes = 0;
    }

    for (int32_t i = 0; i < changes_count; i++) {
        uint16_t change_slot;
        if (!codec.readUint16(change_slot)) return false;
        uint8_t s = clientSlotToServerSlot(window_id, (uint8_t)change_slot);

        uint16_t *p_item = nullptr;
        uint8_t *p_count = nullptr;
        if (s < 41) {
            p_item = &player->inventory_items[s];
            p_count = &player->inventory_count[s];
        } else if (s >= 41 && s <= 49) {
            p_item = &player->craft_items[s - 41];
            p_count = &player->craft_count[s - 41];
        }

        uint8_t has_item;
        if (!codec.readByte(has_item)) return false;
        if (!has_item) {
            if (p_item && apply_changes) { *p_item = 0; *p_count = 0; }
            continue;
        }
        int32_t item_id, item_count, comp_add;
        if (!codec.readVarInt(item_id)) return false;
        if (!codec.readVarInt(item_count)) return false;
        if (!codec.readVarInt(comp_add)) return false;
        if (comp_add < 0 || comp_add > 64) return false;
        for (int32_t c = 0; c < comp_add; c++) {
            int32_t t;
            if (!codec.readVarInt(t)) return false;
            if (!codec.skipBytes(1)) return false;
        }
        int32_t comp_rem;
        if (!codec.readVarInt(comp_rem)) return false;
        if (comp_rem < 0 || comp_rem > 64) return false;
        for (int32_t c = 0; c < comp_rem; c++) {
            int32_t t;
            if (!codec.readVarInt(t)) return false;
        }

        if (item_count > 0 && apply_changes && p_item) {
            *p_item = (uint16_t)item_id;
            *p_count = (uint8_t)item_count;
        }
    }

    if (window_id == 0 || window_id == 12) {
        uint8_t out_count; uint16_t out_item;
        getCraftingOutput(player, &out_count, &out_item);
        sendSetContainerSlot_(pc, window_id, 0, out_count, out_item);
    } else if (window_id == 14) {
        getSmeltingOutput(player);
        for (int i = 0; i < 3; i++)
            sendSetContainerSlot_(pc, window_id, i, player->craft_count[i], player->craft_items[i]);
    }

    uint8_t has_cursor;
    if (!codec.readByte(has_cursor)) return false;
    if (has_cursor) {
        int32_t cursor_item, cursor_count;
        if (!codec.readVarInt(cursor_item)) return false;
        if (!codec.readVarInt(cursor_count)) return false;
        if (apply_changes) {
            player->flagval_16 = (uint16_t)cursor_item;
            player->flagval_8 = (uint8_t)cursor_count;
        }
        int32_t ca;
        if (!codec.readVarInt(ca)) return false;
        if (ca < 0 || ca > 64) return false;
        for (int32_t c = 0; c < ca; c++) {
            int32_t t;
            if (!codec.readVarInt(t)) return false;
            if (!codec.skipBytes(1)) return false;
        }
        int32_t cr;
        if (!codec.readVarInt(cr)) return false;
        if (cr < 0 || cr > 64) return false;
        for (int32_t c = 0; c < cr; c++) {
            int32_t t;
            if (!codec.readVarInt(t)) return false;
        }
    } else {
        if (apply_changes) {
            player->flagval_16 = 0;
            player->flagval_8 = 0;
        }
    }

    return true;
}

bool MinecraftServer::canPlayerEat_(PlayerData* player) {
  if (player->hunger >= 20) return false;
  uint16_t held = player->inventory_items[player->hotbar];
  if (held == 0 || player->inventory_count[player->hotbar] == 0) return false;
  switch (held) {
    case I_chicken: case I_beef: case I_porkchop: case I_mutton:
    case I_cooked_chicken: case I_cooked_beef: case I_cooked_porkchop: case I_cooked_mutton:
    case I_apple:
      return true;
    default: return false;
  }
}

void MinecraftServer::doPlayerEat_(PlayerData* player) {
  if (player->hunger >= 20) return;
  uint16_t *held_item = &player->inventory_items[player->hotbar];
  uint8_t *held_count = &player->inventory_count[player->hotbar];
  if (*held_item == 0 || *held_count == 0) return;

  uint8_t food = 0; uint16_t sat = 0;
  switch (*held_item) {
    case I_chicken: food = 2; sat = 600; break;
    case I_beef: food = 3; sat = 900; break;
    case I_porkchop: food = 3; sat = 300; break;
    case I_mutton: food = 2; sat = 600; break;
    case I_cooked_chicken: food = 6; sat = 3600; break;
    case I_cooked_beef: food = 8; sat = 6400; break;
    case I_cooked_porkchop: food = 8; sat = 6400; break;
    case I_cooked_mutton: food = 6; sat = 4800; break;
    case I_apple: food = 4; sat = 1200; break;
    default: return;
  }

  player->saturation += sat;
  player->hunger += food;
  if (player->hunger > 20) player->hunger = 20;
  *held_count -= 1;
  if (*held_count == 0) *held_item = 0;

  int pi = slotIndexForPlayer_(player);
  if (pi < 0) return;
  PacketCodec pc(clients_[pi].fd);
  sendEntityEvent_(pc, pi, 9);
  sendSetHealth_(pc, player->health, player->hunger, player->saturation);
  sendSetContainerSlot_(pc, 0, serverSlotToClientSlot(0, player->hotbar), *held_count, *held_item);
}

uint8_t MinecraftServer::getArmorItemSlot_(uint16_t item) {
    switch (item) {
        case I_leather_helmet: case I_iron_helmet:
        case I_golden_helmet: case I_diamond_helmet:
        case I_netherite_helmet: return 39;
        case I_leather_chestplate: case I_iron_chestplate:
        case I_golden_chestplate: case I_diamond_chestplate:
        case I_netherite_chestplate: return 38;
        case I_leather_leggings: case I_iron_leggings:
        case I_golden_leggings: case I_diamond_leggings:
        case I_netherite_leggings: return 37;
        case I_leather_boots: case I_iron_boots:
        case I_golden_boots: case I_diamond_boots:
        case I_netherite_boots: return 36;
        default: return 255;
    }
}

void MinecraftServer::broadcastBlockChangesInArea_(int16_t x1, int16_t z1, int16_t x2, int16_t z2) {
  for (int i = 0; i < block_changes_count; i++) {
    if (block_changes[i].block == 0xFF) continue;
    if (block_changes[i].x >= x1 && block_changes[i].x <= x2 &&
        block_changes[i].z >= z1 && block_changes[i].z <= z2) {
      for (uint8_t j = 0; j < kMaxClients; j++) {
        if (!clients_[j].used || clients_[j].state != STATE_PLAY) continue;
        PacketCodec oc(clients_[j].fd);
        sendBlockUpdate_(oc, block_changes[i].x, block_changes[i].y, block_changes[i].z, block_changes[i].block);
      }
    }
  }
}

// ============================================================
// Mob 逻辑
// ============================================================

void MinecraftServer::tickMobs_() {
    for (int i = 0; i < MAX_MOBS; i++) {
        if (mob_data[i].type == 0) continue;
        int entity_id = -2 - i;

        if ((mob_data[i].data & 31) == 0) {
            if (mob_data[i].y < (unsigned int)TICKS_PER_SECOND) { mob_data[i].y++; continue; }
            mob_data[i].type = 0;
            for (uint8_t j = 0; j < kMaxClients; j++) {
                if (!clients_[j].used || clients_[j].state != STATE_PLAY) continue;
                PacketCodec oc(clients_[j].fd);
                sendEntityEvent_(oc, entity_id, 60);
                sendRemoveEntity_(oc, entity_id);
            }
            continue;
        }

        uint8_t passive = (mob_data[i].type == 26 || mob_data[i].type == 30 ||
                           mob_data[i].type == 100 || mob_data[i].type == 111);
        bool is_skeleton = (mob_data[i].type == 115);
        bool is_creeper = (mob_data[i].type == 32);
        bool is_spider = (mob_data[i].type == 124);
        bool is_zombie = (mob_data[i].type == 150);

        PlayerData* closest = nullptr;
        uint32_t closest_dist = 0xFFFFFFFF;
        for (int j = 0; j < MAX_PLAYERS; j++) {
            if (player_data[j].client_fd == -1) continue;
            uint32_t d = abs(mob_data[i].x - player_data[j].x) + abs(mob_data[i].z - player_data[j].z);
            if (d < closest_dist) { closest_dist = d; closest = &player_data[j]; }
        }

        if (closest_dist > MOB_DESPAWN_DISTANCE) { mob_data[i].type = 0; continue; }

        if (is_creeper && closest && closest_dist < 10) {
            uint32_t r = fast_rand();
            if ((r & 31) == 0) {
                for (uint8_t j = 0; j < kMaxClients; j++) {
                    if (!clients_[j].used || clients_[j].state != STATE_PLAY) continue;
                    PacketCodec oc(clients_[j].fd);
                    sendEntityEvent_(oc, entity_id, 1);
                }
                int damage = 20 + (r & 15);
                for (int j = 0; j < MAX_PLAYERS; j++) {
                    if (player_data[j].client_fd == -1) continue;
                    int16_t dx = player_data[j].x - mob_data[i].x;
                    int16_t dz = player_data[j].z - mob_data[i].z;
                    int dist = abs(dx) + abs(dz);
                    if (dist < 20) {
                        uint8_t dmg = damage / (1 + dist / 3);
                        hurtEntity_(j, -1, D_explosion, dmg);
                    }
                }
                for (int dx = -3; dx <= 3; dx++) {
                    for (int dy = -2; dy <= 2; dy++) {
                        for (int dz = -3; dz <= 3; dz++) {
                            if (dx*dx + dy*dy + dz*dz < 9) {
                                uint8_t block = getBlockAt(mob_data[i].x + dx, mob_data[i].y + dy, mob_data[i].z + dz);
                                if (!isPassableBlock(block) && block != B_bedrock && block != B_water && block != B_lava) {
                                    makeBlockChange(mob_data[i].x + dx, mob_data[i].y + dy, mob_data[i].z + dz, B_air);
                                    for (uint8_t j = 0; j < kMaxClients; j++) {
                                        if (!clients_[j].used || clients_[j].state != STATE_PLAY) continue;
                                        PacketCodec oc(clients_[j].fd);
                                        sendBlockUpdate_(oc, mob_data[i].x + dx, mob_data[i].y + dy, mob_data[i].z + dz, B_air);
                                    }
                                }
                            }
                        }
                    }
                }
                for (uint8_t j = 0; j < kMaxClients; j++) {
                    if (!clients_[j].used || clients_[j].state != STATE_PLAY) continue;
                    PacketCodec oc(clients_[j].fd);
                    sendRemoveEntity_(oc, entity_id);
                }
                mob_data[i].type = 0;
                continue;
            }
        }

        if (is_skeleton && closest && closest_dist < 20 && closest_dist > 3) {
            uint32_t r = fast_rand();
            if ((r & 7) == 0) {
                int damage = 4 + (r & 3);
                int target_slot = slotIndexForPlayer_(closest);
                if (target_slot >= 0) hurtEntity_(target_slot, -1, D_arrow, damage);
                for (uint8_t j = 0; j < kMaxClients; j++) {
                    if (!clients_[j].used || clients_[j].state != STATE_PLAY) continue;
                    PacketCodec oc(clients_[j].fd);
                    sendEntityEvent_(oc, entity_id, 4);
                }
                if (closest->x < mob_data[i].x) mob_data[i].x++;
                else if (closest->x > mob_data[i].x) mob_data[i].x--;
                if (closest->z < mob_data[i].z) mob_data[i].z++;
                else if (closest->z > mob_data[i].z) mob_data[i].z--;
                for (uint8_t j = 0; j < kMaxClients; j++) {
                    if (!clients_[j].used || clients_[j].state != STATE_PLAY) continue;
                    PacketCodec oc(clients_[j].fd);
                    sendTeleportEntity_(oc, entity_id, mob_data[i].x + 0.5, mob_data[i].y, mob_data[i].z + 0.5, 0, 0);
                }
                continue;
            }
        }

        if (is_spider && closest && closest_dist < 10) {
            uint8_t above = getBlockAt(mob_data[i].x, mob_data[i].y + 1, mob_data[i].z);
            uint8_t below = getBlockAt(mob_data[i].x, mob_data[i].y - 1, mob_data[i].z);
            if (isPassableBlock(above) && !isPassableBlock(below)) {
                mob_data[i].y++;
                for (uint8_t j = 0; j < kMaxClients; j++) {
                    if (!clients_[j].used || clients_[j].state != STATE_PLAY) continue;
                    PacketCodec oc(clients_[j].fd);
                    sendTeleportEntity_(oc, entity_id, mob_data[i].x + 0.5, mob_data[i].y, mob_data[i].z + 0.5, 0, 0);
                }
            }
        }

        if (is_zombie && closest && closest_dist < 5) {
            uint32_t r = fast_rand();
            if ((r & 127) == 0) {
                int16_t sx = mob_data[i].x + (int16_t)((r & 7) - 3);
                int16_t sz = mob_data[i].z + (int16_t)(((r >> 4) & 7) - 3);
                uint8_t sy = mob_data[i].y;
                spawnMob(150, sx, sy, sz, 20);
                broadcastMobSpawn_(150, sx, sy, sz);
            }
        }

        if (passive) {
            uint32_t r = fast_rand();
            if (r % (4 * (unsigned int)TICKS_PER_SECOND) != 0) continue;
            int16_t new_x = mob_data[i].x, new_z = mob_data[i].z;
            uint8_t yaw = 0;
            if ((r >> 2) & 1) { if ((r >> 1) & 1) { new_x += 1; yaw = 192; } else { new_x -= 1; yaw = 64; } }
            else { if ((r >> 1) & 1) { new_z += 1; yaw = 0; } else { new_z -= 1; yaw = 128; } }
            uint8_t b = getBlockAt(new_x, mob_data[i].y, new_z);
            if (isPassableBlock(b) && !isPassableBlock(getBlockAt(new_x, mob_data[i].y - 1, new_z))) {
                mob_data[i].x = new_x; mob_data[i].z = new_z;
                for (uint8_t j = 0; j < kMaxClients; j++) {
                    if (!clients_[j].used || clients_[j].state != STATE_PLAY) continue;
                    PacketCodec oc(clients_[j].fd);
                    sendTeleportEntity_(oc, entity_id, new_x + 0.5, mob_data[i].y, new_z + 0.5, yaw * 360.0f / 256, 0);
                }
            }
        } else if (!is_skeleton && !is_creeper && !is_spider) {
            if (!closest) continue;
            if (closest_dist < 3 && abs(mob_data[i].y - closest->y) < 2) {
                int ci = slotIndexForPlayer_(closest);
                if (ci >= 0) hurtEntity_(ci, -1, D_generic, 6);
                continue;
            }
            int16_t new_x = mob_data[i].x, new_z = mob_data[i].z;
            if (closest->x < mob_data[i].x) new_x--;
            else if (closest->x > mob_data[i].x) new_x++;
            if (closest->z < mob_data[i].z) new_z--;
            else if (closest->z > mob_data[i].z) new_z++;
            uint8_t b = getBlockAt(new_x, mob_data[i].y, new_z);
            uint8_t b_above = getBlockAt(new_x, mob_data[i].y + 1, new_z);
            uint8_t b_below = getBlockAt(new_x, mob_data[i].y - 1, new_z);
            if (isPassableBlock(b) && isPassableBlock(b_above) && !isPassableBlock(b_below)) {
                mob_data[i].x = new_x; mob_data[i].z = new_z;
                for (uint8_t j = 0; j < kMaxClients; j++) {
                    if (!clients_[j].used || clients_[j].state != STATE_PLAY) continue;
                    PacketCodec oc(clients_[j].fd);
                    sendTeleportEntity_(oc, entity_id, new_x + 0.5, mob_data[i].y, new_z + 0.5, 0, 0);
                }
            }
        }
    }
}

void MinecraftServer::trySpawnMobNearPlayer_(PlayerData* player) {
    if (player->y > 60) {
        int16_t mob_x = 8 + ((fast_rand() & 15) - 7);
        int16_t mob_z = 8 + ((fast_rand() & 15) - 7);
        uint8_t mob_y = getHeightAt(mob_x, mob_z) + 1;
        if (mob_y < 5) mob_y = 5;
        uint32_t r = fast_rand();
        uint8_t type = 26; uint8_t hp = 4;
        if ((r & 3) == 1) { type = 30; hp = 10; }
        else if ((r & 3) == 2) { type = 100; hp = 10; }
        else if ((r & 3) == 3) { type = 111; hp = 8; }
        spawnMob(type, mob_x, mob_y, mob_z, hp);
        broadcastMobSpawn_(type, mob_x, mob_y, mob_z);
        return;
    }
    uint32_t r = fast_rand();
    if ((r & 3) != 0) return;

    int16_t cx = div_floor(player->x, 16);
    int16_t cz = div_floor(player->z, 16);
    int16_t mob_x = (cx + ((r & 1) ? ACTIVE_VIEW_DISTANCE : -ACTIVE_VIEW_DISTANCE)) * 16 + ((r >> 4) & 15);
    int16_t mob_z = (cz + ((r & 2) ? ACTIVE_VIEW_DISTANCE : -ACTIVE_VIEW_DISTANCE)) * 16 + ((r >> 8) & 15);
    uint8_t mob_y = getHeightAt(mob_x, mob_z) + 1;
    if (mob_y < 5) mob_y = 5;

    for (int tries = 0; tries < 20; tries++) {
        uint8_t b_low = getBlockAt(mob_x, mob_y - 1, mob_z);
        uint8_t b_mid = getBlockAt(mob_x, mob_y, mob_z);
        uint8_t b_top = getBlockAt(mob_x, mob_y + 1, mob_z);
        if (!isPassableBlock(b_low) && isPassableSpawnBlock(b_mid) && isPassableSpawnBlock(b_top)) break;
        mob_y++;
        if (mob_y > 250) return;
    }

    if ((world_time < 13000 || world_time > 23460) && mob_y > 48) {
        uint32_t choice = (r >> 12) & 3;
        uint8_t type = 26; uint8_t hp = 4;
        if (choice == 1) { type = 30; hp = 10; }
        else if (choice == 2) { type = 100; hp = 10; }
        else if (choice == 3) { type = 111; hp = 8; }
        spawnMob(type, mob_x, mob_y, mob_z, hp);
        broadcastMobSpawn_(type, mob_x, mob_y, mob_z);
    } else if (mob_y > 48) {
        uint32_t choice = (r >> 12) & 7;
        if (choice <= 3) {
            uint8_t type = 26; uint8_t hp = 4;
            if (choice == 1) { type = 30; hp = 10; }
            else if (choice == 2) { type = 100; hp = 10; }
            else if (choice == 3) { type = 111; hp = 8; }
            spawnMob(type, mob_x, mob_y, mob_z, hp);
            broadcastMobSpawn_(type, mob_x, mob_y, mob_z);
        } else {
            uint8_t hostile_choice = (r >> 16) & 3;
            uint8_t type = 150; uint8_t hp = 20;
            if (hostile_choice == 1) { type = 115; hp = 20; }
            else if (hostile_choice == 2) { type = 124; hp = 16; }
            spawnMob(type, mob_x, mob_y, mob_z, hp);
            broadcastMobSpawn_(type, mob_x, mob_y, mob_z);
        }
    } else {
        uint8_t hostile_choice = (r >> 16) & 3;
        uint8_t type = 150; uint8_t hp = 20;
        if (hostile_choice == 1) { type = 115; hp = 20; }
        else if (hostile_choice == 2) { type = 124; hp = 16; }
        spawnMob(type, mob_x, mob_y, mob_z, hp);
        broadcastMobSpawn_(type, mob_x, mob_y, mob_z);
    }
}

void MinecraftServer::broadcastMobSpawn_(uint8_t type, int16_t x, uint8_t y, int16_t z) {
    int mob_idx = -1;
    for (int i = 0; i < MAX_MOBS; i++) {
        if (mob_data[i].type == type && mob_data[i].x == x && mob_data[i].y == y && mob_data[i].z == z) {
            mob_idx = i; break;
        }
    }
    if (mob_idx < 0) return;

    uint8_t uuid[16];
    uint32_t r = fast_rand();
    memcpy(uuid, &r, 4);
    memcpy(uuid + 4, &mob_idx, 4);
    memset(uuid + 8, 0, 8);
    uint8_t ground_y = getHeightAt(x, z) + 1;
    if (ground_y < 5) ground_y = 5;
    if (y > ground_y + 10 || y < ground_y - 5) {
        y = ground_y;
        mob_data[mob_idx].y = y;
    }

    for (uint8_t j = 0; j < kMaxClients; j++) {
        if (!clients_[j].used || clients_[j].state != STATE_PLAY) continue;
        PacketCodec oc(clients_[j].fd);
        sendSpawnEntity_(oc, -2 - mob_idx, uuid, type, x + 0.5, y, z + 0.5, 0, 0);
    }
}

// ============================================================
// 发包实现: 配置 / 登录 / Play
// ============================================================

bool MinecraftServer::sendBrand_(PacketCodec& codec) {
  const char* brand = "ESP32MC";
  return sendPluginMessage_(codec, "minecraft:brand", (const uint8_t*)brand, (uint32_t)strlen(brand));
}

bool MinecraftServer::sendPluginMessage_(PacketCodec& codec, const char* channel, const uint8_t* data, uint32_t data_len) {
  if (!codec.beginPacket(0x01)) return false;
  if (!codec.writeString(channel)) return false;
  if (!codec.writeVarInt(data_len)) return false;
  if (!codec.writeExact(data, data_len)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendKnownPacks_(PacketCodec& codec) {
  const char* ns = "minecraft"; const char* pack = "core"; const char* ver = VERSION_NAME;
  if (!codec.beginPacket(0x0E)) return false;
  if (!codec.writeVarInt(1)) return false;
  if (!codec.writeString(ns)) return false;
  if (!codec.writeString(pack)) return false;
  if (!codec.writeString(ver)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendEnabledFeatures_(PacketCodec& codec) {
  const char* feature = "minecraft:vanilla";
  if (!codec.beginPacket(0x0C)) return false;
  if (!codec.writeVarInt(1)) return false;
  if (!codec.writeString(feature)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendRegistries_(PacketCodec& codec) {
  return codec.writeExact(registries_bin, sizeof(registries_bin)) &&
         codec.writeExact(tags_bin, sizeof(tags_bin));
}

bool MinecraftServer::sendFinishConfiguration_(PacketCodec& codec) {
  if (!codec.beginPacket(0x03)) return false;
  return codec.endPacket();
}

bool MinecraftServer::consumeClientInformation_(PacketCodec& codec) {
  char locale[32]; uint8_t tmp8; int32_t tmp32;
  if (!codec.readString(locale, sizeof(locale))) return false;
  if (!codec.readByte(tmp8)) return false;
  if (!codec.readVarInt(tmp32)) return false;
  if (!codec.readByte(tmp8)) return false;
  if (!codec.readByte(tmp8)) return false;
  if (!codec.readVarInt(tmp32)) return false;
  if (!codec.readByte(tmp8)) return false;
  if (!codec.readByte(tmp8)) return false;
  if (!codec.readVarInt(tmp32)) return false;
  return true;
}

bool MinecraftServer::consumePluginMessage_(PacketCodec& codec, int32_t payload_len) {
  char channel[64];
  if (!codec.readString(channel, sizeof(channel))) return false;
  int32_t consumed = codec.sizeVarInt((uint32_t)strlen(channel)) + (int32_t)strlen(channel);
  if (payload_len > consumed) return codec.skipBytes((size_t)(payload_len - consumed));
  return true;
}

bool MinecraftServer::consumeKnownPacks_(PacketCodec& codec) {
  int32_t count = 0;
  if (!codec.readVarInt(count)) return false;
  char buf[64];
  for (int32_t i = 0; i < count; i++) {
    if (!codec.readString(buf, sizeof(buf))) return false;
    if (!codec.readString(buf, sizeof(buf))) return false;
    if (!codec.readString(buf, sizeof(buf))) return false;
  }
  return true;
}

bool MinecraftServer::skipRemainingPacket_(PacketCodec& codec, int32_t packet_len, int32_t packet_id) {
  int32_t remaining = packet_len - codec.sizeVarInt((uint32_t)packet_id);
  if (remaining <= 0) return true;
  return codec.skipBytes((size_t)remaining);
}

bool MinecraftServer::sendLoginPlay_(PacketCodec& codec, uint32_t entity_id) {
  const char* dim = "overworld";
  if (!codec.beginPacket(0x31)) return false;
  if (!codec.writeUint32(entity_id)) return false;
  if (!codec.writeByte(0)) return false;
  if (!codec.writeVarInt(1)) return false;
  if (!codec.writeVarInt(9)) return false;
  if (!codec.writeExact((const uint8_t*)dim, 9)) return false;
  if (!codec.writeVarInt(MAX_PLAYERS)) return false;
  if (!codec.writeVarInt(ACTIVE_VIEW_DISTANCE)) return false;
  if (!codec.writeVarInt(ACTIVE_VIEW_DISTANCE)) return false;
  if (!codec.writeByte(0)) return false;
  if (!codec.writeByte(1)) return false;
  if (!codec.writeByte(0)) return false;
  if (!codec.writeVarInt(0)) return false;
  if (!codec.writeVarInt(9)) return false;
  if (!codec.writeExact((const uint8_t*)dim, 9)) return false;
  if (!codec.writeUint64(0x0123456789ABCDEFULL)) return false;
  if (!codec.writeByte(GAMEMODE)) return false;
  if (!codec.writeByte(0xFF)) return false;
  if (!codec.writeByte(0)) return false;
  if (!codec.writeByte(0)) return false;
  if (!codec.writeByte(0)) return false;
  if (!codec.writeVarInt(0)) return false;
  if (!codec.writeVarInt(63)) return false;
  if (!codec.writeByte(0)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendSynchronizePlayerPosition_(PacketCodec& codec, double x, double y, double z, float yaw, float pitch) {
  if (!codec.beginPacket(0x48)) return false;
  if (!codec.writeVarInt((uint32_t)-1)) return false;
  if (!codec.writeDouble(x)) return false;
  if (!codec.writeDouble(y)) return false;
  if (!codec.writeDouble(z)) return false;
  if (!codec.writeDouble(0)) return false;
  if (!codec.writeDouble(0)) return false;
  if (!codec.writeDouble(0)) return false;
  if (!codec.writeFloat(yaw)) return false;
  if (!codec.writeFloat(pitch)) return false;
  if (!codec.writeUint32(0)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendSetDefaultSpawnPosition_(PacketCodec& codec, int64_t x, int64_t y, int64_t z, float yaw, float pitch) {
  const char* dim = "minecraft:overworld";
  uint32_t dim_len = (uint32_t)strlen(dim);
  uint64_t packed = (((uint64_t)x & 0x3FFFFFFULL) << 38) | (((uint64_t)z & 0x3FFFFFFULL) << 12) | ((uint64_t)y & 0xFFFULL);
  if (!codec.beginPacket(0x61)) return false;
  if (!codec.writeVarInt(dim_len)) return false;
  if (!codec.writeExact((const uint8_t*)dim, dim_len)) return false;
  if (!codec.writeUint64(packed)) return false;
  if (!codec.writeFloat(yaw)) return false;
  if (!codec.writeFloat(pitch)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendStartWaitingForChunks_(PacketCodec& codec) {
  if (!codec.beginPacket(0x26)) return false;
  if (!codec.writeVarInt(13)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendSetCenterChunk_(PacketCodec& codec, int x, int z) {
  if (!codec.beginPacket(0x5E)) return false;
  if (!codec.writeVarInt((uint32_t)x)) return false;
  if (!codec.writeVarInt((uint32_t)z)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendKeepAlive_(PacketCodec& codec) {
  if (!codec.beginPacket(0x2C)) return false;
  if (!codec.writeUint64(0)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendSetHealth_(PacketCodec& codec, uint8_t health, uint8_t food, uint16_t saturation) {
  if (!codec.beginPacket(0x68)) return false;
  if (!codec.writeFloat((float)health)) return false;
  if (!codec.writeVarInt(food)) return false;
  if (!codec.writeFloat((float)(saturation - 200) / 500.0f)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendSetHeldItem_(PacketCodec& codec, uint8_t slot) {
  if (!codec.beginPacket(0x69)) return false;
  if (!codec.writeByte(slot)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendSetContainerSlot_(PacketCodec& codec, int window_id, uint16_t slot, uint8_t count, uint16_t item) {
  if (slot >= 46) return false;
  if (count == 0) item = 0;
  if (!codec.beginPacket(0x14)) return false;
  if (!codec.writeVarInt((uint32_t)window_id)) return false;
  if (!codec.writeVarInt(0)) return false;
  if (!codec.writeUint16(slot)) return false;
  if (!codec.writeVarInt(count)) return false;
  if (count > 0) {
    if (!codec.writeVarInt(item)) return false;
    if (!codec.writeVarInt(0)) return false;
    if (!codec.writeVarInt(0)) return false;
  }
  return codec.endPacket();
}

bool MinecraftServer::sendBlockUpdate_(PacketCodec& codec, int64_t x, int64_t y, int64_t z, uint8_t block) {
  if (!codec.beginPacket(0x08)) return false;
  if (!codec.writeUint64(((x & 0x3FFFFFF) << 38) | ((z & 0x3FFFFFF) << 12) | (y & 0xFFF))) return false;
  if (!codec.writeVarInt(block_palette[block])) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendAcknowledgeBlockChange_(PacketCodec& codec, int sequence) {
  if (!codec.beginPacket(0x04)) return false;
  if (!codec.writeVarInt((uint32_t)sequence)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendPlayerInfoUpdateAddPlayer_(PacketCodec& codec, PlayerData& player) {
  uint32_t name_len = (uint32_t)strlen(player.name);
  uint8_t actions = 0x01 | 0x08 | 0x10;
  if (!codec.beginPacket(0x46)) return false;
  if (!codec.writeByte(actions)) return false;
  if (!codec.writeVarInt(1)) return false;
  if (!codec.writeExact(player.uuid, 16)) return false;
  if (!codec.writeVarInt(name_len)) return false;
  if (!codec.writeExact((const uint8_t*)player.name, name_len)) return false;
  if (!codec.writeVarInt(0)) return false;
  if (!codec.writeByte(1)) return false;
  if (!codec.writeVarInt(0)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendSpawnEntity_(PacketCodec& codec, int id, uint8_t* uuid, int type, double x, double y, double z, uint8_t yaw, uint8_t pitch) {
  if (!codec.beginPacket(0x01)) return false;
  if (!codec.writeVarInt((uint32_t)id)) return false;
  if (!codec.writeExact(uuid, 16)) return false;
  if (!codec.writeVarInt((uint32_t)type)) return false;
  if (!codec.writeDouble(x)) return false;
  if (!codec.writeDouble(y)) return false;
  if (!codec.writeDouble(z)) return false;
  if (!codec.writeByte(0)) return false;
  if (!codec.writeByte(pitch)) return false;
  if (!codec.writeByte(yaw)) return false;
  if (!codec.writeByte(yaw)) return false;
  if (!codec.writeVarInt(0)) return false;
  if (!codec.writeUint16(0)) return false;
  if (!codec.writeUint16(0)) return false;
  if (!codec.writeUint16(0)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendEntityAnimation_(PacketCodec& codec, int id, uint8_t animation) {
  if (!codec.beginPacket(0x02)) return false;
  if (!codec.writeVarInt((uint32_t)id)) return false;
  if (!codec.writeByte(animation)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendTeleportEntity_(PacketCodec& codec, int id, double x, double y, double z, float yaw, float pitch) {
  if (!codec.beginPacket(0x7D)) return false;
  if (!codec.writeVarInt((uint32_t)id)) return false;
  if (!codec.writeDouble(x)) return false;
  if (!codec.writeDouble(y)) return false;
  if (!codec.writeDouble(z)) return false;
  if (!codec.writeDouble(0)) return false;
  if (!codec.writeDouble(0)) return false;
  if (!codec.writeDouble(0)) return false;
  if (!codec.writeFloat(yaw)) return false;
  if (!codec.writeFloat(pitch)) return false;
  if (!codec.writeUint32(1)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendSetHeadRotation_(PacketCodec& codec, int id, uint8_t yaw) {
  if (!codec.beginPacket(0x53)) return false;
  if (!codec.writeVarInt((uint32_t)id)) return false;
  if (!codec.writeByte(yaw)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendUpdateEntityRotation_(PacketCodec& codec, int id, uint8_t yaw, uint8_t pitch) {
  if (!codec.beginPacket(0x38)) return false;
  if (!codec.writeVarInt((uint32_t)id)) return false;
  if (!codec.writeByte(yaw)) return false;
  if (!codec.writeByte(pitch)) return false;
  if (!codec.writeByte(1)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendDamageEvent_(PacketCodec& codec, int entity_id, int type) {
  if (!codec.beginPacket(0x19)) return false;
  if (!codec.writeVarInt((uint32_t)entity_id)) return false;
  if (!codec.writeVarInt((uint32_t)type)) return false;
  if (!codec.writeByte(0)) return false;
  if (!codec.writeByte(0)) return false;
  if (!codec.writeByte(0)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendRemoveEntity_(PacketCodec& codec, int entity_id) {
  if (!codec.beginPacket(0x4D)) return false;
  if (!codec.writeByte(1)) return false;
  if (!codec.writeVarInt((uint32_t)entity_id)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendSystemChat_(PacketCodec& codec, const char* message, uint16_t len) {
  if (len == 0 || message == nullptr || message[0] == '\0') return true;
  if (!codec.beginPacket(0x79)) return false;
  if (!codec.writeByte(8)) return false;
  if (!codec.writeUint16(len)) return false;
  if (!codec.writeExact((const uint8_t*)message, len)) return false;
  if (!codec.writeByte(0)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendEntityEvent_(PacketCodec& codec, int entity_id, uint8_t status) {
  if (!codec.beginPacket(0x22)) return false;
  if (!codec.writeUint32((uint32_t)entity_id)) return false;
  if (!codec.writeByte(status)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendOpenScreen_(PacketCodec& codec, uint8_t window, const char* title, uint16_t length) {
  if (!codec.beginPacket(0x3B)) return false;
  if (!codec.writeVarInt(window)) return false;
  if (!codec.writeVarInt(window)) return false;
  if (!codec.writeByte(8)) return false;
  if (!codec.writeUint16(length)) return false;
  if (!codec.writeExact((const uint8_t*)title, length)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendRespawn_(PacketCodec& codec) {
  const char* dim = "overworld";
  if (!codec.beginPacket(0x52)) return false;
  if (!codec.writeVarInt(0)) return false;
  if (!codec.writeVarInt(9)) return false;
  if (!codec.writeExact((const uint8_t*)dim, 9)) return false;
  if (!codec.writeUint64(0x0123456789ABCDEFULL)) return false;
  if (!codec.writeByte(GAMEMODE)) return false;
  if (!codec.writeByte(0xFF)) return false;
  if (!codec.writeByte(0)) return false;
  if (!codec.writeByte(0)) return false;
  if (!codec.writeByte(0)) return false;
  if (!codec.writeVarInt(0)) return false;
  if (!codec.writeVarInt(63)) return false;
  if (!codec.writeByte(0)) return false;
  return codec.endPacket();
}

bool MinecraftServer::sendPlayerAbilities_(PacketCodec& codec, uint8_t flags) {
  if (!codec.beginPacket(0x40)) return false;
  if (!codec.writeByte(flags)) return false;
  if (!codec.writeFloat(0.05f)) return false;
  if (!codec.writeFloat(0.1f)) return false;
  return codec.endPacket();
}

// ============================================================
// 区块数据
// ============================================================

bool MinecraftServer::sendChunkDataAndUpdateLight_(PacketCodec& codec, int chunk_x, int chunk_z) {
    if (chunk_x < -30000000 || chunk_x > 30000000 || chunk_z < -30000000 || chunk_z > 30000000) {
        return true;
    }

    static const int TOTAL_SECTIONS = 24;
    static const int EMPTY_BELOW = 4;
    static const int GEN_SECTIONS = 6;
    static const int EMPTY_ABOVE = TOTAL_SECTIONS - EMPTY_BELOW - GEN_SECTIONS;
    static const int SKY_LIGHT_SECTIONS = 18;
    static const int DARK_SECTIONS = 6;
    static const int BRIGHT_SECTIONS = SKY_LIGHT_SECTIONS - DARK_SECTIONS;

    int cx = chunk_x * 16, cz = chunk_z * 16;

    // ====== 开始包 ======
    if (!codec.beginPacket(0x2D)) return false;
    if (!codec.writeUint32((uint32_t)chunk_x)) return false;
    if (!codec.writeUint32((uint32_t)chunk_z)) return false;

    // ====== 先写临时数据到 buffer（用 endPacket 会自动算长度） ======
    // 由于无法预知 chunk_data_size，直接写入字段
    // 这里借用 codec 内部 buffer 的能力

    // 写一个占位 varint 表示 chunk_data_size（稍后重新构造整个包）
    // 由于 beginPacket 已经把 packet_id 写进 buffer，现在可以继续写

    // 由于无法回填 chunk_data_size，改用两阶段：
    // 1. 先构造完整包到本地 buffer
    // 2. 再 beginPacket + 写字段 + endPacket

    static uint8_t enc[5000];
    static uint64_t longs_buf[512];
    static uint16_t sec_enc_lens[6];

    int bedrock_sec_size = 2 + 2 + 1 + codec.sizeVarInt(block_palette[B_bedrock]) + 1 + 1;
    int air_sec_size = 2 + 2 + 1 + codec.sizeVarInt(block_palette[B_air]) + 1 + 1;
    int chunk_data_size = bedrock_sec_size * EMPTY_BELOW + air_sec_size * EMPTY_ABOVE;

    for (int sec = 0; sec < GEN_SECTIONS; sec++) {
        buildChunkSection(cx, sec * 16, cz);
        uint16_t palette_len = 0;
        uint16_t non_air = 0;
        uint8_t seen[256] = {};
        for (int j = 0; j < 4096; j++) {
            uint8_t b = chunk_section[j];
            if (b != B_air) non_air++;
            if (!seen[b]) { seen[b] = 1; palette_len++; }
        }
        uint8_t bits = 0;
        if (palette_len > 1) {
            uint16_t m = palette_len - 1;
            while (m > 0) { bits++; m >>= 1; }
            if (bits < 4) bits = 4;
        }
        int size = 2 + 2 + 1;
        if (bits == 0) {
            for (int j = 0; j < 256; j++) if (seen[j]) {
                size += codec.sizeVarInt(block_palette[j]);
                break;
            }
        } else {
            size += codec.sizeVarInt(palette_len);
            for (int j = 0; j < 256; j++) if (seen[j])
                size += codec.sizeVarInt(block_palette[j]);
            size += (int)bits * 64 * 8;
        }
        size += 2;
        sec_enc_lens[sec] = (uint16_t)size;
        chunk_data_size += size;
    }

    int light_data_size = 9 + 1 + 9 + 1 + 1 + BRIGHT_SECTIONS * (2 + 2048) + 1;
    int pkt_id_size = codec.sizeVarInt(0x2D);
    int data_len_size = codec.sizeVarInt((uint32_t)chunk_data_size);
    uint32_t total_pkt_len = pkt_id_size + 4 + 4 + 1 + data_len_size + chunk_data_size + 1 + light_data_size;

    if (total_pkt_len <= 0 || total_pkt_len > 2000000) {
        codec.abortPacket();
        Serial.printf("[CHUNK_ERR] chunk(%d,%d) total_pkt_len=%u, sending empty\n",
                      chunk_x, chunk_z, (unsigned)total_pkt_len);
        if (!codec.beginPacket(0x2D)) return false;
        if (!codec.writeUint32((uint32_t)chunk_x)) return false;
        if (!codec.writeUint32((uint32_t)chunk_z)) return false;
        if (!codec.writeVarInt(0)) return false;
        if (!codec.writeVarInt(0)) return false;
        return codec.endPacket();
    }

    // 写 chunk_data 的占位大小
    if (!codec.writeVarInt((uint32_t)chunk_data_size)) return false;

    // 写 sections
    for (int i = 0; i < EMPTY_BELOW; i++) {
        if (!codec.writeUint16(4096) || !codec.writeUint16(0) || !codec.writeByte(0)) return false;
        if (!codec.writeVarInt(block_palette[B_bedrock]) || !codec.writeByte(0) || !codec.writeByte(0)) return false;
    }

    for (int sec = 0; sec < GEN_SECTIONS; sec++) {
        uint8_t biome = buildChunkSection(cx, sec * 16, cz);
        uint16_t palette_index[256];
        uint8_t palette_blocks[256];
        uint16_t palette_len = 0, non_air = 0;
        memset(palette_index, 0xFF, sizeof(palette_index));
        for (int j = 0; j < 4096; j++) {
            uint8_t b = chunk_section[j];
            if (b != B_air) non_air++;
            if (palette_index[b] == 0xFFFF) {
                palette_index[b] = palette_len;
                palette_blocks[palette_len] = b;
                palette_len++;
            }
        }
        uint8_t bits = 0;
        if (palette_len > 1) {
            uint16_t m = palette_len - 1;
            while (m > 0) { bits++; m >>= 1; }
            if (bits < 4) bits = 4;
        }

        size_t used = 0;
        enc[used++] = (uint8_t)(non_air >> 8);
        enc[used++] = (uint8_t)non_air;
        enc[used++] = 0;
        enc[used++] = 0;
        enc[used++] = bits;
        if (bits == 0) {
            uint32_t val = block_palette[palette_blocks[0]];
            do {
                uint8_t b = val & 0x7F;
                val >>= 7;
                if (val) b |= 0x80;
                enc[used++] = b;
            } while (val);
        } else {
            uint32_t pl = palette_len;
            do {
                uint8_t b = pl & 0x7F;
                pl >>= 7;
                if (pl) b |= 0x80;
                enc[used++] = b;
            } while (pl);
            for (uint16_t p = 0; p < palette_len; p++) {
                uint32_t val = block_palette[palette_blocks[p]];
                do {
                    uint8_t b = val & 0x7F;
                    val >>= 7;
                    if (val) b |= 0x80;
                    enc[used++] = b;
                } while (val);
            }
            uint32_t long_count = (uint32_t)bits * 64;
            memset(longs_buf, 0, long_count * 8);
            for (int j = 0; j < 4096; j++) {
                uint64_t value = palette_index[chunk_section[j]];
                uint32_t bit_index = (uint32_t)j * bits;
                size_t word_index = bit_index >> 6;
                uint8_t bit_offset = bit_index & 63;
                longs_buf[word_index] |= value << bit_offset;
                if (bit_offset + bits > 64)
                    longs_buf[word_index + 1] |= value >> (64 - bit_offset);
            }
            for (uint32_t j = 0; j < long_count; j++) {
                uint64_t be = __builtin_bswap64(longs_buf[j]);
                memcpy(enc + used, &be, 8);
                used += 8;
            }
        }
        enc[used++] = 0;
        enc[used++] = biome;

        if (!codec.writeExact(enc, sec_enc_lens[sec])) return false;
        yield();
    }

    for (int i = 0; i < EMPTY_ABOVE; i++) {
        if (!codec.writeUint16(0) || !codec.writeUint16(0) || !codec.writeByte(0)) return false;
        if (!codec.writeVarInt(block_palette[B_air]) || !codec.writeByte(0) || !codec.writeByte(0)) return false;
    }
    if (!codec.writeVarInt(0)) return false;

    // 光照
    uint64_t sky_light_mask = 0;
    uint64_t empty_sky_mask = 0;
    for (int i = 0; i < DARK_SECTIONS; i++) empty_sky_mask |= (1ULL << i);
    for (int i = DARK_SECTIONS; i < SKY_LIGHT_SECTIONS; i++) sky_light_mask |= (1ULL << i);

    if (!codec.writeVarInt(1) || !codec.writeUint64(sky_light_mask)) return false;
    if (!codec.writeVarInt(0)) return false;
    if (!codec.writeVarInt(1) || !codec.writeUint64(empty_sky_mask)) return false;
    if (!codec.writeVarInt(0)) return false;

    if (!codec.writeVarInt(BRIGHT_SECTIONS)) return false;
    memset(chunk_section, 0xFF, 2048);
    for (int i = 0; i < BRIGHT_SECTIONS; i++) {
        if (!codec.writeVarInt(2048) || !codec.writeExact(chunk_section, 2048)) return false;
        yield();
    }
    if (!codec.writeVarInt(0)) return false;

    // ====== 结束包 ======
    if (!codec.endPacket()) {
        Serial.printf("[CHUNK_ERR] chunk(%d,%d) endPacket failed\n", chunk_x, chunk_z);
        return false;
    }

    // 区块改动
    for (int i = 0; i < block_changes_count; i++) {
        if (block_changes[i].block == 0xFF) continue;
        int bx = block_changes[i].x, bz = block_changes[i].z;
        if (bx >= cx && bx < cx + 16 && bz >= cz && bz < cz + 16)
            sendBlockUpdate_(codec, block_changes[i].x, block_changes[i].y, block_changes[i].z, block_changes[i].block);
    }

    return true;
}

// ============================================================
// 辅助方法
// ============================================================

uint16_t MinecraftServer::onlineCount_() const {
  uint16_t count = 0;
  for (uint8_t i = 0; i < kMaxClients; i++)
    if (clients_[i].used && clients_[i].state == STATE_PLAY) count++;
  return count;
}

PacketCodec MinecraftServer::codecForSlot_(uint8_t slot_index) {
  return PacketCodec(clients_[slot_index].fd);
}

int MinecraftServer::slotIndexForPlayer_(PlayerData* player) {
  for (uint8_t i = 0; i < kMaxClients; i++) {
    if (clients_[i].player_index >= 0 && &player_data[clients_[i].player_index] == player) return i;
  }
  return -1;
}

void MinecraftServer::processDeferredChunks_(uint8_t slot_index) {
  ClientSlot& slot = clients_[slot_index];
  if (slot.chunk_queue_idx < 0) return;
  if (slot.fd < 0) { slot.chunk_queue_idx = -1; return; }

  int side = ACTIVE_VIEW_DISTANCE * 2 + 1;
  int total = side * side - 1;

  if (slot.chunk_queue_idx >= total) {
    slot.chunk_queue_idx = -1;
    if (slot.player_index >= 0) {
      PlayerData* player = &player_data[slot.player_index];
      PacketCodec pc(slot.fd);
      sendSynchronizePlayerPosition_(pc, player->x + 0.5, player->y, player->z + 0.5,
        player->yaw * 180.0f / 127, player->pitch * 90.0f / 127);
      for (uint8_t i = 0; i < 41; i++)
        sendSetContainerSlot_(pc, 0, serverSlotToClientSlot(0, i), player->inventory_count[i], player->inventory_items[i]);
      sendSetHeldItem_(pc, player->hotbar);
      sendSetHealth_(pc, player->health, player->hunger, player->saturation);
      {
        if (!pc.beginPacket(0x83)) return;
        if (!pc.writeVarInt((uint32_t)(slot.player_index))) return;
        if (!pc.writeVarInt(1)) return;
        if (!pc.writeVarInt(22)) return;
        if (!pc.writeDouble(0.1)) return;
        if (!pc.writeVarInt(0)) return;
        pc.endPacket();
      }
    }
    return;
  }

  int idx = slot.chunk_queue_idx;
  int raw = idx;
  int center_offset = ACTIVE_VIEW_DISTANCE * side + ACTIVE_VIEW_DISTANCE;
  if (raw >= center_offset) raw++;
  int ox = (raw / side) - ACTIVE_VIEW_DISTANCE;
  int oz = (raw % side) - ACTIVE_VIEW_DISTANCE;

  PacketCodec pc(slot.fd);
  sendChunkDataAndUpdateLight_(pc, slot.chunk_center_x + ox, slot.chunk_center_z + oz);

  slot.chunk_queue_idx++;
}
