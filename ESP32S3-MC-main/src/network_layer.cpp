#ifndef _WIN32

#include "network_layer.h"

NetworkLayer::NetworkLayer(uint16_t port)
    : server_(port),
      server_started_(false),
      ip_announced_(false),
      ssid_(nullptr),
      password_(nullptr),
      next_retry_at_ms_(0) {}

bool NetworkLayer::begin(const char* ssid, const char* password, uint32_t connect_timeout_ms) {
  ssid_ = ssid;
  password_ = password;
  next_retry_at_ms_ = 0;
  ip_announced_ = false;

  // AP 模式
  if (ssid_ == nullptr || ssid_[0] == '\0') {
    Serial.println("AP mode: Starting server directly");
    startServer_();
    return true;
  }

  // STA 模式
  Serial.println("STA mode: Starting server first, then connecting WiFi");
  startServer_();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid_);

  WiFi.begin(ssid_, password_ != nullptr ? password_ : "");

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start >= connect_timeout_ms) {
      Serial.println("WiFi connect timeout, will retry in background");
      next_retry_at_ms_ = millis() + 5000;
      break;
    }
    if (WiFi.status() == WL_CONNECT_FAILED) {
      Serial.println("WiFi connect failed (wrong password?)");
      next_retry_at_ms_ = millis() + 10000;
      break;
    }
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected! IP: ");
    Serial.println(WiFi.localIP());
    ip_announced_ = true;
  }

  return true;
}

void NetworkLayer::poll() {
  // ====== AP 模式 ======
  if (ssid_ == nullptr || ssid_[0] == '\0') {
    if (!server_started_) {
      startServer_();
    }
    return;
  }

  // ====== STA 模式 ======
  
  // 已连接
  if (WiFi.status() == WL_CONNECTED) {
    if (!server_started_) {
      startServer_();
    }
    if (!ip_announced_) {
      Serial.print("WiFi connected, IP: ");
      Serial.println(WiFi.localIP());
      ip_announced_ = true;
    }
    return;
  }

  // 排除"正在连接"的状态
  wl_status_t status = WiFi.status();
  if (status == WL_IDLE_STATUS || status == WL_SCAN_COMPLETED) {
    return;
  }

  // 记录断开
  if (ip_announced_) {
    Serial.printf("WiFi disconnected (status=%d), reconnecting...\n", status);
    ip_announced_ = false;
  }

  // 重连延时
  uint32_t now = millis();
  if (next_retry_at_ms_ != 0 && (int32_t)(now - next_retry_at_ms_) < 0) {
    return;
  }

  // 重连前先断开，清理 WiFi 栈状态
  if (status == WL_CONNECT_FAILED || status == WL_CONNECTION_LOST) {
    WiFi.disconnect(true);
    delay(100);
  }

  next_retry_at_ms_ = now + 5000;
  Serial.printf("[WiFi] Reconnecting to %s\n", ssid_);
  WiFi.begin(ssid_, password_ != nullptr ? password_ : "");
}

bool NetworkLayer::connected() const {
  if (ssid_ == nullptr || ssid_[0] == '\0') {
    return true;
  }
  return WiFi.status() == WL_CONNECTED;
}

WiFiClient NetworkLayer::accept() {
  if (ssid_ == nullptr || ssid_[0] == '\0') {
    if (!server_started_) {
      startServer_();
    }
    return server_.available();
  }

  if (WiFi.status() != WL_CONNECTED) {
    return WiFiClient();
  }

  if (!server_started_) {
    startServer_();
  }

  return server_.available();
}

bool NetworkLayer::connectWiFi_(uint32_t connect_timeout_ms) {
  // 保留兼容性，实际逻辑已移到 begin()
  return WiFi.status() == WL_CONNECTED;
}

void NetworkLayer::startServer_() {
  if (server_started_) {
    return;
  }

  server_.begin();
  server_.setNoDelay(true);
  server_started_ = true;

  Serial.println("Server listening on port 25565");
}

#endif
