#ifndef PACKET_CODEC_H
#define PACKET_CODEC_H

#include <stdint.h>
#include <stddef.h>

// 有数据收发时触发，由平台层（code.ino）赋值
extern void (*g_packet_activity_cb)();

class PacketCodec {
 public:
  explicit PacketCodec(int fd);

  bool readByte(uint8_t& value);
  bool writeByte(uint8_t value);
  bool readUint16(uint16_t& value);
  bool writeUint16(uint16_t value);
  bool readUint32(uint32_t& value);
  bool writeUint32(uint32_t value);
  bool readUint64(uint64_t& value);
  bool writeUint64(uint64_t value);
  bool readFloat(float& value);
  bool writeFloat(float value);
  bool readDouble(double& value);
  bool writeDouble(double value);

  bool readExact(uint8_t* buf, size_t len);
  bool writeExact(const uint8_t* buf, size_t len);
  bool readVarInt(int32_t& value);
  bool writeVarInt(uint32_t value);
  int sizeVarInt(uint32_t value) const;
  bool readString(char* out, size_t out_len);
  bool writeString(const char* str);
  bool skipString();
  bool skipBytes(size_t len);

  int fd() const { return fd_; }

  // 写字节计数 (用于调试包长度)
  void resetWriteCount() { write_count_ = 0; }
  size_t writeCount() const { return write_count_; }

  // 设置写超时 (ms), 0 = 使用默认 5000ms
  void setWriteTimeout(uint32_t ms) { write_timeout_ms_ = ms; }
  bool writeTimedOut() const { return write_timed_out_; }

 private:
  int fd_;
  uint32_t write_timeout_ms_ = 5000;
  bool write_timed_out_ = false;
  size_t write_count_ = 0;
};

#endif
