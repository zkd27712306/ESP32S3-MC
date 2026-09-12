#include "packet_codec.h"
#include <string.h>

void (*g_packet_activity_cb)() = nullptr;

#ifdef _WIN32
#include "win_platform.h"
#else
#include <Arduino.h>
#include <errno.h>
#include <lwip/sockets.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace {
const uint8_t SEGMENT_BITS = 0x7F;
const uint8_t CONTINUE_BIT = 0x80;

// ====== 全局包缓冲 ======
static uint8_t g_packet_buf[65536];
static size_t g_packet_buf_len = 0;
static bool g_packet_open = false;
}

PacketCodec::PacketCodec(int fd) : fd_(fd) {}

// ============================================================
// 包级封装
// ============================================================

bool PacketCodec::beginPacket(uint32_t packet_id) {
    if (g_packet_open) return false;
    g_packet_buf_len = 0;
    g_packet_open = true;
    write_count_ = 0;

    uint32_t v = packet_id;
    do {
        uint8_t b = v & 0x7F;
        v >>= 7;
        if (v) b |= 0x80;
        if (g_packet_buf_len >= sizeof(g_packet_buf)) {
            g_packet_open = false;
            packet_error_count_++;
            return false;
        }
        g_packet_buf[g_packet_buf_len++] = b;
    } while (v);

    return true;
}

bool PacketCodec::endPacket() {
    if (!g_packet_open) return false;
    g_packet_open = false;

    uint32_t length = (uint32_t)g_packet_buf_len;
    if (length == 0) {
        packet_error_count_++;
        return false;
    }

    uint8_t len_bytes[5];
    size_t len_len = 0;
    uint32_t v = length;
    do {
        uint8_t b = v & 0x7F;
        v >>= 7;
        if (v) b |= 0x80;
        len_bytes[len_len++] = b;
    } while (v);

    if (!writeExact(len_bytes, len_len)) return false;
    if (!writeExact(g_packet_buf, g_packet_buf_len)) return false;

    g_packet_buf_len = 0;
    return true;
}

void PacketCodec::abortPacket() {
    g_packet_open = false;
    g_packet_buf_len = 0;
}

// ============================================================
// 底层读写
// ============================================================

bool PacketCodec::readExact(uint8_t* buf, size_t len) {
  size_t done = 0;
  uint32_t start = millis();
  while (done < len) {
    if (fd_ < 0) return false;
#ifdef _WIN32
    int n = recv((SOCKET)fd_, (char*)(buf + done), (int)(len - done), 0);
    if (n > 0) { done += (size_t)n; read_count_ += (size_t)n; start = millis(); continue; }
    if (n == 0) return false;
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK || err == WSAEINTR) {
      if (millis() - start > 5000) return false;
      Sleep(1);
      continue;
    }
#else
    int n = recv(fd_, buf + done, len - done, 0);
    if (n > 0) {
      done += (size_t)n;
      read_count_ += (size_t)n;
      start = millis();
      if (g_packet_activity_cb) g_packet_activity_cb();
      continue;
    }
    if (n == 0) return false;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      if (millis() - start > 5000) return false;
      vTaskDelay(1);
      continue;
    }
#endif
    return false;
  }
  return true;
}

bool PacketCodec::writeExact(const uint8_t* buf, size_t len) {
    if (len == 0) return true;
    if (buf == nullptr) {
        packet_error_count_++;
        write_timed_out_ = true;
        return false;
    }

    // ====== 包内：写 buffer ======
    if (g_packet_open) {
        if (g_packet_buf_len + len > sizeof(g_packet_buf)) {
            packet_error_count_++;
            return false;
        }
        memcpy(g_packet_buf + g_packet_buf_len, buf, len);
        g_packet_buf_len += len;
        write_count_ += len;
        return true;
    }

    // ====== 包外：直接写 socket ======
    write_timed_out_ = false;
    write_count_ += len;
    size_t done = 0;
    uint32_t start = millis();
    while (done < len) {
        if (fd_ < 0) return false;
        size_t chunk = len - done;
        if (chunk > 1460) chunk = 1460;
#ifdef _WIN32
        int n = send((SOCKET)fd_, (const char*)(buf + done), (int)chunk, 0);
        if (n > 0) { done += (size_t)n; start = millis(); continue; }
        if (n == 0) return false;
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAEINTR) {
            if (millis() - start > write_timeout_ms_) { write_timed_out_ = true; return false; }
            Sleep(1);
            continue;
        }
#else
        int n = send(fd_, buf + done, chunk, MSG_NOSIGNAL);
        if (n > 0) {
            done += (size_t)n;
            start = millis();
            if (g_packet_activity_cb) g_packet_activity_cb();
            continue;
        }
        if (n == 0) return false;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            if (millis() - start > write_timeout_ms_) { write_timed_out_ = true; return false; }
            vTaskDelay(1);
            continue;
        }
#endif
        return false;
    }
    return true;
}

bool PacketCodec::readByte(uint8_t& value) { return readExact(&value, 1); }
bool PacketCodec::writeByte(uint8_t value) { return writeExact(&value, 1); }

bool PacketCodec::readUint16(uint16_t& value) {
  uint8_t buf[2];
  if (!readExact(buf, 2)) return false;
  value = ((uint16_t)buf[0] << 8) | buf[1];
  return true;
}
bool PacketCodec::writeUint16(uint16_t value) {
  uint8_t buf[2] = {(uint8_t)(value >> 8), (uint8_t)value};
  return writeExact(buf, 2);
}

bool PacketCodec::readUint32(uint32_t& value) {
  uint8_t buf[4];
  if (!readExact(buf, 4)) return false;
  value = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | buf[3];
  return true;
}
bool PacketCodec::writeUint32(uint32_t value) {
  uint8_t buf[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value};
  return writeExact(buf, 4);
}

bool PacketCodec::readUint64(uint64_t& value) {
  uint8_t buf[8];
  if (!readExact(buf, 8)) return false;
  value = ((uint64_t)buf[0] << 56) | ((uint64_t)buf[1] << 48) | ((uint64_t)buf[2] << 40) | ((uint64_t)buf[3] << 32) |
          ((uint64_t)buf[4] << 24) | ((uint64_t)buf[5] << 16) | ((uint64_t)buf[6] << 8) | (uint64_t)buf[7];
  return true;
}
bool PacketCodec::writeUint64(uint64_t value) {
  uint8_t buf[8] = {
    (uint8_t)(value >> 56), (uint8_t)(value >> 48), (uint8_t)(value >> 40), (uint8_t)(value >> 32),
    (uint8_t)(value >> 24), (uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value};
  return writeExact(buf, 8);
}

bool PacketCodec::readFloat(float& value) {
  uint32_t bits; if (!readUint32(bits)) return false;
  memcpy(&value, &bits, 4); return true;
}
bool PacketCodec::writeFloat(float value) {
  uint32_t bits; memcpy(&bits, &value, 4); return writeUint32(bits);
}
bool PacketCodec::readDouble(double& value) {
  uint64_t bits; if (!readUint64(bits)) return false;
  memcpy(&value, &bits, 8); return true;
}
bool PacketCodec::writeDouble(double value) {
  uint64_t bits; memcpy(&bits, &value, 8); return writeUint64(bits);
}

bool PacketCodec::readVarInt(int32_t& value) {
  value = 0; int position = 0;
  while (true) {
    uint8_t byte = 0;
    if (!readExact(&byte, 1)) return false;
    value |= (byte & SEGMENT_BITS) << position;
    if ((byte & CONTINUE_BIT) == 0) return true;
    position += 7;
    if (position >= 32) return false;
  }
}

bool PacketCodec::writeVarInt(uint32_t value) {
    uint8_t out[5];
    size_t len = 0;
    do {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        if (value) byte |= 0x80;
        out[len++] = byte;
    } while (value);

    if (len == 0) return true;
    return writeExact(out, len);
}

bool PacketCodec::writePacketLength(uint32_t value) {
    if (value == 0) {
        packet_error_count_++;
        write_timed_out_ = true;
        return false;
    }
    return writeVarInt(value);
}

int PacketCodec::sizeVarInt(uint32_t value) const {
    int size = 1;
    while ((value & ~0x7F) != 0) {
        value >>= 7;
        size++;
    }
    return size;
}

bool PacketCodec::readString(char* out, size_t out_len) {
  int32_t len = 0;
  if (!readVarInt(len) || len < 0 || out_len == 0) return false;
  size_t need = (size_t)len, copy_len = need;
  if (copy_len >= out_len) copy_len = out_len - 1;
  if (copy_len > 0 && !readExact((uint8_t*)out, copy_len)) return false;
  out[copy_len] = '\0';
  if (need > copy_len && !skipBytes(need - copy_len)) return false;
  return true;
}

bool PacketCodec::writeString(const char* str) {
    size_t len = strlen(str);
    if (len == 0) {
        return writeVarInt(0);
    }
    return writeVarInt((uint32_t)len) && writeExact((const uint8_t*)str, len);
}

bool PacketCodec::skipString() {
  int32_t len = 0;
  if (!readVarInt(len) || len < 0) return false;
  return skipBytes((size_t)len);
}

bool PacketCodec::skipBytes(size_t len) {
  uint8_t tmp[64];
  while (len > 0) { size_t chunk = len > 64 ? 64 : len; if (!readExact(tmp, chunk)) return false; len -= chunk; }
  return true;
}
