#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_system.h>
#include "game_state.h"
#include "mc_server.h"

static const uint16_t MC_PORT = 25565;
static MinecraftServer server(MC_PORT);
static bool server_started = false;

#define BOOT_BUTTON_PIN  0
#define SHORT_PRESS_MS   2000
#define LONG_PRESS_MS    10000
#define DEBOUNCE_MS      50

static Preferences prefs;
static WebServer webServer(80);

static String wifi_ssid;
static String wifi_pass;
static bool use_ap_mode = true;

static const char* SETUP_SSID = "ESP32-MC-Setup";
static const char* SETUP_PASS = "12345678";

static const char* HTML_PAGE = R"(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-MC Setup</title>
<style>
body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:20px;background:#f5f5f5}
h1{color:#333;margin-bottom:5px}
p.sub{color:#888;margin-top:0;font-size:14px}
form{background:white;padding:20px;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,0.1)}
label{display:block;margin-top:12px;font-weight:bold;color:#555}
input{width:100%;padding:10px;margin:6px 0;box-sizing:border-box;font-size:16px;border:1px solid #ccc;border-radius:4px}
button{width:100%;padding:12px;background:#4CAF50;color:white;border:none;font-size:16px;cursor:pointer;border-radius:4px;margin-top:16px}
button:hover{background:#45a049}
.radio-group{margin-top:12px}
.radio-group label{display:block;font-weight:normal;margin:6px 0;cursor:pointer}
.radio-group input{width:auto;margin-right:8px}
</style>
</head>
<body>
<h1>ESP32-MC Setup</h1>
<p class="sub">Configure WiFi and server mode</p>
<form action="/save" method="POST">
<label>WiFi Name (SSID)</label>
<input type="text" name="ssid" required maxlength="32" placeholder="Your WiFi name">
<label>WiFi Password</label>
<input type="password" name="pass" maxlength="64" placeholder="Your WiFi password">
<div class="radio-group">
<label><input type="radio" name="mode" value="sta" checked> STA - Connect to your WiFi</label>
<label><input type="radio" name="mode" value="ap"> AP - Create ESP32-MC hotspot</label>
</div>
<button type="submit">Save & Reboot</button>
</form>
</body>
</html>
)";

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

static void loadConfig() {
    prefs.begin("esp32mc", true);
    wifi_ssid = prefs.getString("ssid", "");
    wifi_pass = prefs.getString("pass", "");
    use_ap_mode = prefs.getBool("ap_mode", true);
    prefs.end();
}

static void saveConfig(const String& ssid, const String& pass, bool ap) {
    prefs.begin("esp32mc", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.putBool("ap_mode", ap);
    prefs.putBool("configured", true);
    prefs.end();
}

static void saveApMode(bool ap) {
    prefs.begin("esp32mc", false);
    prefs.putBool("ap_mode", ap);
    prefs.end();
}

static bool isConfigured() {
    prefs.begin("esp32mc", true);
    bool c = prefs.getBool("configured", false);
    prefs.end();
    return c;
}

static void clearConfig() {
    prefs.begin("esp32mc", false);
    prefs.clear();
    prefs.end();
}

static void handleRoot() {
    webServer.send(200, "text/html", HTML_PAGE);
}

static void handleSave() {
    String ssid = webServer.arg("ssid");
    String pass = webServer.arg("pass");
    String mode = webServer.arg("mode");

    if (ssid.length() == 0 && mode != "ap") {
        webServer.send(400, "text/plain", "SSID required for STA mode");
        return;
    }

    bool ap = (mode == "ap");
    saveConfig(ssid, pass, ap);

    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Saved</title>";
    html += "<style>body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:20px}</style></head><body>";
    html += "<h1>Saved</h1>";
    html += "<p>Mode: " + String(ap ? "AP" : "STA") + "</p>";
    if (!ap) html += "<p>WiFi: " + ssid + "</p>";
    html += "<p>ESP32 will reboot in 2 seconds...</p>";
    html += "</body></html>";
    webServer.send(200, "text/html", html);

    delay(2000);
    ESP.restart();
}

static void startSetupAP() {
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);
    WiFi.softAP(SETUP_SSID, SETUP_PASS);
    IPAddress ip = WiFi.softAPIP();

    Serial.println("========================================");
    Serial.println("  WiFi Setup Mode");
    Serial.println("========================================");
    Serial.print("  WiFi: ");
    Serial.println(SETUP_SSID);
    Serial.print("  Pass: ");
    Serial.println(SETUP_PASS);
    Serial.print("  URL:  http://");
    Serial.println(ip);
    Serial.println("  Long-press BOOT 10s to reset");
    Serial.println("========================================");

    webServer.on("/", handleRoot);
    webServer.on("/save", HTTP_POST, handleSave);
    webServer.begin();
}

static bool isBootPressed() {
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    delay(50);
    return digitalRead(BOOT_BUTTON_PIN) == LOW;
}

static void checkBootButton() {
    static uint32_t press_start = 0;
    static bool pressed = false;

    bool current = (digitalRead(BOOT_BUTTON_PIN) == LOW);

    if (current && !pressed) {
        delay(DEBOUNCE_MS);
        if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
            pressed = true;
            press_start = millis();
            Serial.println("[BOOT] Pressed");
        }
    } else if (current && pressed) {
        uint32_t held = millis() - press_start;

        if (held >= LONG_PRESS_MS) {
            Serial.println("[BOOT] 10s -> clear config, entering setup mode");
            clearConfig();
            delay(100);
            ESP.restart();
        }
    } else if (!current && pressed) {
        uint32_t held = millis() - press_start;
        pressed = false;

        if (held >= SHORT_PRESS_MS && held < LONG_PRESS_MS) {
            Serial.println("[BOOT] 2s release -> toggle AP/STA");
            saveApMode(!use_ap_mode);
            delay(100);
            ESP.restart();
        } else {
            Serial.printf("[BOOT] Released after %u ms (ignored)\n", held);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

    esp_reset_reason_t reset_reason = esp_reset_reason();
    Serial.println("\n========================================");
    Serial.println("  ESP32-MC Server");
    Serial.println("  Protocol: 26.1.2 / 775");
    Serial.println("========================================");
    Serial.printf("Reset: %d (%s)\n", (int)reset_reason, resetReasonString(reset_reason));
    Serial.printf("Free heap: %u\n", ESP.getFreeHeap());

    loadConfig();

    if (!isConfigured()) {
        Serial.println("[MODE] Not configured -> setup mode");
        startSetupAP();
        return;
    }

    uint32_t seed = esp_random();
    seed ^= (uint32_t)micros() << 16;
    seed ^= (uint32_t)millis() << 8;
    seed ^= (uint32_t)esp_random() << 24;
    if (seed == 0) seed = 0xDEADBEEF;
    world_seed = seed;
    rng_seed = seed ^ 0x350B10FB;

    Serial.printf("World Seed: 0x%08X\n", world_seed);
    Serial.printf("RNG Seed: 0x%08X\n", rng_seed);

    if (use_ap_mode) {
        Serial.println("[MODE] AP");
        WiFi.mode(WIFI_AP);
        WiFi.setSleep(false);
        WiFi.softAP("ESP32-MC", "ESP32-MC", 6, 0, 5);
        IPAddress ip = WiFi.softAPIP();
        Serial.print("AP IP: ");
        Serial.println(ip);
        if (!server.begin("", "")) {
            Serial.println("[ERROR] Server failed to start");
        } else {
            Serial.println("[OK] Server started");
            server_started = true;
        }
    } else {
        Serial.printf("[MODE] STA -> %s\n", wifi_ssid.c_str());
        if (!server.begin(wifi_ssid.c_str(), wifi_pass.c_str())) {
            Serial.println("[ERROR] Server failed to start");
        } else {
            Serial.println("[OK] Server started");
            server_started = true;
        }
    }

    Serial.println("========================================");
    if (use_ap_mode) {
        Serial.println("  Mode: AP");
        Serial.println("  WiFi: ESP32-MC / ESP32-MC");
        Serial.println("  Server: 192.168.4.1:25565");
    } else {
        Serial.println("  Mode: STA");
        Serial.printf("  WiFi: %s\n", wifi_ssid.c_str());
    }
    Serial.println("  BOOT 2s  -> switch AP/STA");
    Serial.println("  BOOT 10s -> reset config");
    Serial.println("========================================");
}

void loop() {
    checkBootButton();

    if (server_started) {
        server.poll();
    } else {
        webServer.handleClient();
    }

    if (use_ap_mode && server_started) {
        static uint32_t last_check = 0;
        if (millis() - last_check > 5000) {
            last_check = millis();
            WiFi.setSleep(false);
        }
    }

    vTaskDelay(1);
}
