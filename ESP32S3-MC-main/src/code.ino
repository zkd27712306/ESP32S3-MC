/**
 * ESP32MC - Minecraft Java Server on ESP32
 * 协议版本 26.1.2 / 775
 * WiFi STA 模式，连接外部 WiFi 热点
 * 每次启动生成不同世界
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include "game_state.h"
#include "mc_server.h"

static const uint16_t MC_PORT = 25565;
static MinecraftServer server(MC_PORT);
static bool server_started = false;

// ============================================================
// WiFi 配置（请修改为你的路由器信息）
// ============================================================
static const char* WIFI_SSID     = "zhengkaidong";
static const char* WIFI_PASSWORD = "zkd27712306zkd";

static const char *resetReasonString(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:   return "unknown";
        case ESP_RST_POWERON:   return "power_on";
        case ESP_RST_EXT:       return "external";
        case ESP_RST_SW:        return "software";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_INT_WDT:   return "int_wdt";
        case ESP_RST_TASK_WDT:  return "task_wdt";
        case ESP_RST_WDT:       return "other_wdt";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "sdio";
        default:                return "unmapped";
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    esp_reset_reason_t reset_reason = esp_reset_reason();
    Serial.println("\n========================================");
    Serial.println("  ESP32-MC Server (STA Mode)");
    Serial.println("  Protocol: 26.1.2 / 775");
    Serial.println("========================================");
    Serial.printf("Reset reason: %d (%s)\n", (int)reset_reason, resetReasonString(reset_reason));
    Serial.printf("Free heap on boot: %u bytes\n", ESP.getFreeHeap());

    // ====== 生成随机世界种子 ======
    uint32_t seed = esp_random();
    seed ^= (uint32_t)micros() << 16;
    seed ^= (uint32_t)millis() << 8;
    seed ^= (uint32_t)esp_random() << 24;
    if (seed == 0) seed = 0xDEADBEEF;
    world_seed = seed;
    rng_seed = seed ^ 0x350B10FB;

    Serial.printf("World Seed: 0x%08X\n", world_seed);
    Serial.printf("RNG Seed: 0x%08X\n", rng_seed);

    // ====== 启动服务器（先启动，再连接 WiFi） ======
    if (!server.begin(WIFI_SSID, WIFI_PASSWORD)) {
        Serial.println("[ERROR] Server failed to start!");
    } else {
        Serial.println("[OK] Server initialized!");
        server_started = true;
    }

    // ====== 打印连接提示 ======
    Serial.println("========================================");
    Serial.print("  Connect with Minecraft Java 26.1.2 to the ESP32 IP");
    Serial.println();
    Serial.print("  SSID: ");
    Serial.println(WIFI_SSID);
    Serial.print("  Port: ");
    Serial.println(MC_PORT);
    Serial.println("========================================");
}

void loop() {
    if (server_started) {
        server.poll();
    }
    vTaskDelay(1);
}
