# ESP32MC Server 🌐 [中文](https://github.com/zkd27712306/ESP32S3-MC/blob/main/README.md)

> One ESP32-S3 is a Minecraft Java server.

A minimalist Minecraft Java server optimized from [ESP32-MC](https://github.com/GYGKHD/ESP32-MC), running on the **ESP32-S3**. It fixes a large number of original bugs (such as container-opening crashes, empty packet sending, furnace recursion, etc.) and adds several practical features (such as the `!give` command, armor system, bow system, mob AI, etc.).

---

## 📜 Open Source License and Origin

| Project | Description |
|---------|-------------|
| Original Project | [ESP32-MC](https://github.com/GYGKHD/ESP32-MC) |
| Original Author | GYGKHD and all contributors who participated in code modifications |
| Original License | GPL-3.0 |
| This Project's License | GPL-3.0 |
| Derivative Modifications | Completed by [zkd27712306](https://github.com/zkd27712306) in 2026 |

See `LICENSE` for the full license, and `NOTICE` for origin and modification statements.

### Main Modifications in This Derivative Project

- Fixed container UI crash when opening
- Fixed entity event packet length error
- Fixed empty packet sending causing zero frame length
- Fixed furnace recursion stack overflow risk
- Fixed chest storage loss / inability to open
- Fixed armor slots incorrectly participating in item stacking
- World generation: generate a random world seed on each startup
- Game features: added netherite equipment, armor system, bow system, infinite water bucket, torch placement
- Mob AI: added creeper explosion, skeleton arrow shooting, zombie group attacks
- Network mode: first power-on enters Setup hotspot + Web configuration; after configuration, enters AP or STA mode based on selection

---

## 🎯 Project Positioning

> A minimalist Minecraft Java server running on an ESP32-S3.

**Protocol Version**: `26.1.2 / 775`

The overall approach is to use direct, traceable implementations as much as possible, compressing Minecraft Java's basic multiplayer and survival logic onto a chip with very tight resources.

### ✅ Priorities

- Runs stably on ESP32-S3
- Code structure as direct as possible, easy to continue modifying
- Easy to locate when problems occur

### ⏸️ Not Prioritized

- Complete vanilla features
- High concurrency
- Plugin compatibility
- Overly wrapped engineering structure

---

## ✨ Features Overview

| Category | Content |
|----------|---------|
| 🎮 Gameplay | Login, spawning, movement, chat, block placement/destruction, simple fluids |
| 📦 Systems | Inventory, basic crafting, furnace, chest storage |
| 🛡️ Combat | Armor system (armor value / toughness / damage reduction), bow shooting |
| 🐷 Mobs | Basic mob spawning, creeper explosion, skeleton arrow shooting, zombie group attacks |
| 🌍 World | Basic chunk generation, terrain, biomes, random world seed |
| 🌐 Network | First power-on enters Setup hotspot + Web configuration; after configuration, enters AP or STA mode based on selection |

---

## 🚀 Quick Start

### Method 1: Direct Firmware Download (Recommended, No Build Needed)

Download the ready-to-flash firmware directly from [ESP32S3-MC Releases](https://github.com/zkd27712306/ESP32S3-MC/releases) and flash it using a flashing tool (such as [ESPWebTool](https://esptool.spacehuhn.com/)).

### Method 2: Cloud Build (No Local Environment Needed)

You can fork this project first for better compilation.

This project supports [GitHub Actions](https://github.com/zkd27712306/ESP32S3-MC/actions) cloud compilation, no need to install PlatformIO locally.

1. Open the repository's [Actions page](https://github.com/zkd27712306/ESP32S3-MC/actions)
2. Select **ESP32-S3 Cloud Build** on the left
3. Click **Run workflow** on the right
4. After compilation completes, download `esp32s3-firmware.zip` from the **Artifacts** section at the bottom of the run record

> ⚠️ Artifacts are valid for 30 days, please download promptly.

### Method 3: Local Build

1. Open the `ESP32S3-MC-main/src/` directory with Visual Studio Code
2. Install PlatformIO; this project uses `4d_systems_esp32s3_gen4_r8n16` by default (ESP32-S3 + 8MB PSRAM)
3. **Please use the repository's included `platformio.ini`, do not arbitrarily change the board type** — boards without PSRAM may have insufficient memory and cannot run stably
4. Compile and flash

---

## 🔥 Flash Addresses (Bootloader 0x0)

> ⚠️ The ESP32-S3's bootloader start address is `0x0`, **not** the ESP32's `0x1000`.

When flashing with [ESPWebTool](https://esptool.spacehuhn.com/) or esptool, please fill in the addresses as follows:

| File | Flash Address |
|------|---------------|
| `bootloader.bin` | `0x0` |
| `partitions.bin` | `0x8000` |
| `firmware.bin` | `0x10000` |

### ESPWebTool Web Flashing

[ESPWebTool](https://esptool.spacehuhn.com/) is a browser-based ESP flashing tool that requires no software installation; flash directly in Chrome / Edge browsers.

**Web Address**: [ESPWebTool](https://esptool.spacehuhn.com/)

**Flashing Steps**:

1. Open [ESPWebTool](https://esptool.spacehuhn.com/) with Chrome or Edge
2. Click the **Connect** button and select the ESP32-S3's serial port (COMx on Windows)
3. Enter the address in **Flash Address**, select the corresponding file in **File**
4. Add the 3 files in order according to the correspondence below:

| File | Flash Address |
|------|---------------|
| `bootloader.bin` | `0x0` |
| `partitions.bin` | `0x8000` |
| `firmware.bin` | `0x10000` |

5. Click **Program** to start flashing
6. Wait for the progress bar to finish; **Done** indicates successful flashing
7. Press the **RST / EN** button on the ESP32-S3 board, or power cycle

> 💡 **If [ESPWebTool](https://esptool.spacehuhn.com/) cannot connect to the development board, check**:
> - Whether the browser is Chrome / Edge (Firefox, Safari do not support Web Serial)
> - Whether the USB cable is a data cable (not a pure charging cable)
> - Whether the driver is installed (CH340 / CP2102 / ESP32-S3 built-in USB)
> - Whether the BOOT button is held before clicking Connect

---

## 📡 Operating Modes

### First Power-On (Unconfigured)

1. Power on the ESP32-S3
2. Automatically enters **Setup configuration mode**, creating WiFi hotspot `ESP32-MC-Setup`
3. Password: `12345678`
4. Connect to `ESP32-MC-Setup` with a phone or computer
5. Open `192.168.4.1` in a browser
6. Fill in the WiFi name and password to connect, or select AP / STA mode
7. After saving, the ESP32 automatically restarts and enters the corresponding mode:
   - Select **STA** → Connects to your configured WiFi, serial prints the router-assigned IP (e.g., `192.168.101.91`); Minecraft client connects using that IP + `:25565`
   - Select **AP** → Creates game hotspot `ESP32-MC`, IP `192.168.4.1`; Minecraft client connects using `192.168.4.1:25565`

> First power-on requires a one-time Web configuration; subsequent power-ons will run according to the saved mode.

### Subsequent Power-On (Configured)

**AP Mode (Default):**

1. Power on the ESP32-S3
2. Automatically creates game hotspot `ESP32-MC`, password `ESP32-MC`
3. Server listens on `192.168.4.1:25565`
4. Minecraft client connects to `192.168.4.1:25565` to play

**STA Mode:**

1. Power on the ESP32-S3
2. Automatically connects to the saved WiFi
3. Serial prints the router-assigned IP (e.g., `192.168.101.91`)
4. Minecraft client connects to that IP + `:25565`

> To switch between AP / STA: long-press BOOT for **2 seconds**; the ESP32 will switch modes and restart.

### BOOT Button Operations

| Operation | Effect |
|-----------|--------|
| Long-press BOOT for **2 seconds** | Switch between AP / STA mode (when configured) |
| Long-press BOOT for **10 seconds** | Clear WiFi configuration; restart returns to first power-on Setup mode |

### Connecting to Minecraft

1. Open Minecraft Java Edition **26.1.2**
2. Go to **Multiplayer → Add Server**
3. Fill in the address:
   - AP mode: `192.168.4.1:25565`
   - STA mode: The IP assigned to the ESP32 by the router + `:25565`
4. Click **Join Server**

---

## ⚙️ Current Capabilities

- Player login, spawning, movement, chat
- Basic chunk generation, terrain and biomes
- Block placement, destruction, simple fluids
- Inventory, basic crafting, furnace logic
- Basic mob spawning and partial behaviors
- Armor system (armor value / toughness / damage reduction)
- Bow shooting system
- Infinite water bucket
- Chest storage
- First power-on enters Setup hotspot, configure WiFi and mode via Web
- Supports AP / STA operating modes, BOOT button switching
- BOOT button configuration reset

### Default Configuration

| Parameter | Value |
|-----------|-------|
| Max Players | `5` |
| View Distance | `2` |
| Default Port | `25565` |
| AP Hotspot | `ESP32-MC` / `ESP32-MC` |
| Setup Hotspot | `ESP32-MC-Setup` / `12345678` |

These values and most switches are defined in [`ESP32S3-MC-main/src/game_types.h`](ESP32S3-MC-main/src/game_types.h).

---

## ⚠️ Known Limitations

- Requires **ESP32-S3 with PSRAM** (8MB recommended); boards without PSRAM may have insufficient memory and cannot run stably
- View distance fixed at 2, max 5 players
- No redstone, no villages, no Nether, no End
- Bows have no parabola, no line-of-sight detection
- Furnace only supports partial recipes
- This firmware has Task Watchdog (Task WDT) disabled; after a crash it will not auto-restart, requiring manual RST press

---

## 📁 Directory Structure

The main code is all under the `ESP32S3-MC-main/src/` directory:

- [`ESP32S3-MC-main/src/code.ino`](ESP32S3-MC-main/src/code.ino): Arduino entry point, initializes serial, WiFi, Web configuration, and main loop
- [`ESP32S3-MC-main/src/mc_server.cpp`](ESP32S3-MC-main/src/mc_server.cpp): Server core, connection management, protocol state machine, main game logic
- [`ESP32S3-MC-main/src/mc_server.h`](ESP32S3-MC-main/src/mc_server.h): Server class header
- [`ESP32S3-MC-main/src/packet_codec.cpp`](ESP32S3-MC-main/src/packet_codec.cpp): Minecraft packet encoding/decoding
- [`ESP32S3-MC-main/src/packet_codec.h`](ESP32S3-MC-main/src/packet_codec.h): Codec header
- [`ESP32S3-MC-main/src/network_layer.cpp`](ESP32S3-MC-main/src/network_layer.cpp): ESP32 network layer wrapper
- [`ESP32S3-MC-main/src/network_layer.h`](ESP32S3-MC-main/src/network_layer.h): Network layer header
- [`ESP32S3-MC-main/src/procedures.cpp`](ESP32S3-MC-main/src/procedures.cpp): Player behavior, block interaction, mob and tick logic
- [`ESP32S3-MC-main/src/procedures.h`](ESP32S3-MC-main/src/procedures.h): Procedure function header
- [`ESP32S3-MC-main/src/terrain.cpp`](ESP32S3-MC-main/src/terrain.cpp): Terrain, chunk, and basic structure generation
- [`ESP32S3-MC-main/src/terrain.h`](ESP32S3-MC-main/src/terrain.h): Terrain generation header
- [`ESP32S3-MC-main/src/crafting.cpp`](ESP32S3-MC-main/src/crafting.cpp): Crafting and furnace logic
- [`ESP32S3-MC-main/src/crafting.h`](ESP32S3-MC-main/src/crafting.h): Crafting header
- [`ESP32S3-MC-main/src/game_state.cpp`](ESP32S3-MC-main/src/game_state.cpp): Global game state
- [`ESP32S3-MC-main/src/game_state.h`](ESP32S3-MC-main/src/game_state.h): Game state header
- [`ESP32S3-MC-main/src/game_types.h`](ESP32S3-MC-main/src/game_types.h): Main constants, switches, and data structures
- [`ESP32S3-MC-main/src/registries.cpp`](ESP32S3-MC-main/src/registries.cpp): Protocol registries and related large data
- [`ESP32S3-MC-main/src/registries.h`](ESP32S3-MC-main/src/registries.h): Registry header

---

## 🎮 In-Game Commands

| Command | Description |
|---------|-------------|
| `!help` | Show help |
| `!items` | List all giveable items |
| `!give <player> <item> [count]` | Give item to player |
| `!summon <mob> [count]` | Spawn mob |
| `!msg <player> <message>` | Private message |
| `!overworld` | Return to spawn point |

### Available Mobs

`chicken`, `cow`, `pig`, `sheep`, `zombie`, `skeleton`, `spider`, `creeper`

---

## 🛠️ Development Notes

- The current mainline code is based on `ESP32S3-MC-main/src`.
- `registries.cpp / registries.h` are large in size, mainly protocol-related static data
- Many designs in this project are for saving resources and simplifying debugging, not necessarily pursuing the complete abstraction of common server implementations

---

## 🔗 Related Links

| Name | Link |
|------|------|
| GitHub Repository | [zkd27712306/ESP32S3-MC](https://github.com/zkd27712306/ESP32S3-MC) |
| Actions Page | [ESP32S3-MC Actions](https://github.com/zkd27712306/ESP32S3-MC/actions) |
| Releases Download | [ESP32S3-MC Releases](https://github.com/zkd27712306/ESP32S3-MC/releases) |
| Original Project | [ESP32-MC](https://github.com/GYGKHD/ESP32-MC) |
| ESPWebTool Web Flashing | [ESPWebTool](https://esptool.spacehuhn.com/) |

---

## 📄 License

This project is licensed under the **GPL-3.0** license. See the `LICENSE` file for details.
