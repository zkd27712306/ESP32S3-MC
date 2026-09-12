#ifndef PACKET_CODEC_H
#define PACKET_CODEC_H

#include <stdint.h>
#include <stddef.h>

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
  bool writePacketLength(uint32_t value);
  int sizeVarInt(uint32_t value) const;
  bool readString(char* out, size_t out_len);
  bool writeString(const char* str);
  bool skipString();
  bool skipBytes(size_t len);

  // ====== 包级封装 ======
  bool beginPacket(uint32_t packet_id);
  bool endPacket();
  void abortPacket();

  int fd() const { return fd_; }

  void resetWriteCount() { write_count_ = 0; }
  size_t writeCount() const { return write_count_; }

  void resetReadCount() { read_count_ = 0; }
  size_t readCount() const { return read_count_; }

  void setWriteTimeout(uint32_t ms) { write_timeout_ms_ = ms; }
  bool writeTimedOut() const { return write_timed_out_; }

  uint32_t packetErrorCount() const { return packet_error_count_; }
  void resetPacketErrorCount() { packet_error_count_ = 0; }

 private:
  int fd_;
  uint32_t write_timeout_ms_ = 5000;
  bool write_timed_out_ = false;
  size_t write_count_ = 0;
  size_t read_count_ = 0;
  uint32_t packet_error_count_ = 0;
};

#endif
