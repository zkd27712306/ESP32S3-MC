#ifndef NETWORK_LAYER_H
#define NETWORK_LAYER_H

#include <Arduino.h>
#include <WiFi.h>

class NetworkLayer {
 public:
  explicit NetworkLayer(uint16_t port);

  bool begin(const char* ssid, const char* password, uint32_t connect_timeout_ms = 15000);
  void poll();
  bool connected() const;
  WiFiClient accept();

 private:
  bool connectWiFi_(uint32_t connect_timeout_ms);
  void startServer_();

  WiFiServer server_;
  bool server_started_;
  bool ip_announced_;
  const char* ssid_;
  const char* password_;
  uint32_t next_retry_at_ms_;
};

#endif
