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

  // ====== AP 模式（SSID 为空）：网络栈由 softAP 隐式初始化，可直接启动服务器 ======
  if (ssid_ == nullptr || ssid_[0] == '\0') {
    Serial.println("AP mode: Starting server directly");
    startServer_();
    return true;
  }

  // ====== STA 模式：必须先连上 WiFi，再启动服务器 ======
  Serial.println("STA mode: Connecting WiFi first, then starting server");

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
      return true;   // 服务器延后到 poll() 里启动
    }
    if (WiFi.status() == WL_CONNECT_FAILED) {
      Serial.println("WiFi connect failed (wrong password?)");
      next_retry_at_ms_ = millis() + 10000;
      return true;   // 服务器延后到 poll() 里启动
    }
    delay(250);
  }

  Serial.print("WiFi connected! IP: ");
  Serial.println(WiFi.localIP());
  ip_announced_ = true;

  // WiFi 就绪后才创建监听 socket
  startServer_();
  return true;
}

void NetworkLayer::poll() {
  // ====== AP 模式：不需要处理 WiFi 重连 ======
  if (ssid_ == nullptr || ssid_[0] == '\0') {
    if (!server_started_) {
      startServer_();
    }
    return;
  }

  // ====== STA 模式：已连接 ======
  if (WiFi.status() == WL_CONNECTED) {
    if (!server_started_) {
      // 首次连接超时后由 poll() 兜底启动
      startServer_();
    }
    if (!ip_announced_) {
      Serial.print("WiFi connected, IP: ");
      Serial.println(WiFi.localIP());
      ip_announced_ = true;
    }
    return;
  }

  // ====== STA 模式：正在连接中，不干预 ======
  wl_status_t status = WiFi.status();
  if (status == WL_IDLE_STATUS || status == WL_SCAN_COMPLETED) {
    return;
  }

  // ====== STA 模式：已断开，准备重连 ======
  if (ip_announced_) {
    Serial.printf("WiFi disconnected (status=%d), reconnecting...\n", status);
    ip_announced_ = false;
  }

  uint32_t now = millis();
  if (next_retry_at_ms_ != 0 && (int32_t)(now - next_retry_at_ms_) < 0) {
    return;
  }

  // 连接失败/丢失时先彻底断开，清理 WiFi 栈状态
  if (status == WL_CONNECT_FAILED || status == WL_CONNECTION_LOST) {
    WiFi.disconnect(true);
    delay(100);
  }

  next_retry_at_ms_ = now + 5000;
  Serial.printf("[WiFi] Reconnecting to %s\n", ssid_);
  WiFi.begin(ssid_, password_ != nullptr ? password_ : "");
}

bool NetworkLayer::connected() const {
  // AP 模式下始终返回 true
  if (ssid_ == nullptr || ssid_[0] == '\0') {
    return true;
  }
  return WiFi.status() == WL_CONNECTED;
}

WiFiClient NetworkLayer::accept() {
  // AP 模式下直接接受连接
  if (ssid_ == nullptr || ssid_[0] == '\0') {
    if (!server_started_) {
      startServer_();
    }
    return server_.available();
  }

  // STA 模式：未连接时不接受
  if (WiFi.status() != WL_CONNECTED) {
    return WiFiClient();
  }

  if (!server_started_) {
    startServer_();
  }

  return server_.available();
}

bool NetworkLayer::connectWiFi_(uint32_t connect_timeout_ms) {
  if (ssid_ == nullptr || ssid_[0] == '\0') {
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid_, password_ != nullptr ? password_ : "");

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start >= connect_timeout_ms) {
      Serial.println("WiFi connect timeout");
      return false;
    }
    delay(250);
  }

  ip_announced_ = true;
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());
  return true;
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

#endif // !_WIN32
