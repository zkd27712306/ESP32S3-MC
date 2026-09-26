# ESP32MC Server 🌐 [中文](https://github.com/zkd27712306/ESP32S3-MC/blob/main/README.md)

> One ESP32-S3 is a Minecraft Java server.

A minimalist Minecraft Java server optimized from [ESP32-MC](https://github.com/GYGKHD/ESP32-MC), running on the **ESP32-S3**. It fixes a large number of original bugs (such as crashes when opening containers, empty packet sending, furnace recursion, etc.) and adds some practical features (such as the `!give` command, armor system, bow and arrow system, mob AI, etc.).

---

## 📜 Open Source License and Origin

| Item | Description |
|------|-------------|
| Original project | [ESP32-MC](https://github.com/GYGKHD/ESP32-MC) |
| Original project author | GYGKHD and all contributors who participated in code modifications |
| Original project license | GPL-3.0 |
| This project license | GPL-3.0 |
| Derivative modifications | Completed by [zkd27712306](https://github.com/zkd27712306) in 2026 |

See `LICENSE` for the full license, and `NOTICE` for origin and modification statements.

### Main modifications of this derivative project

- Fixed crashes when opening container interfaces
- Fixed incorrect entity event packet length
- Fixed empty packet sending causing zero frame length
- Fixed furnace recursion stack overflow risk
- Fixed chest storage loss / inability to open
- Fixed armor slots incorrectly participating in item stacking
- World generation: generate a random world seed on each startup
- Game features: added netherite equipment, armor system, bow and arrow system, infinite water bucket, torch placement
- Mob AI: added creeper explosion, skeleton archery, zombie swarm attacks
- Network mode: first power-on enters Setup hotspot + Web configuration; after configuration, choose to enter AP or STA mode

---

## 🎯 Project Positioning

> A minimalist Minecraft Java server running on an ESP32-S3.

**Protocol version**: `26.1.2 / 775`

The overall approach is to use direct, traceable implementations as much as possible, compressing the basic multiplayer and survival logic of Minecraft Java onto a chip with very limited resources.

### ✅ Priorities

- Run stably on ESP32-S3
- Keep the code structure as direct as possible for easy further modification
- Make problems easy to locate

### ⏸️ Not currently prioritized

- Complete vanilla features
- High concurrency
- Plugin compatibility
- Overly packaged engineering structure

---

## ✨ Feature Overview

| Category | Content |
|----------|---------|
| 🎮 Gameplay | Login, spawn, movement, chat, block placement/destruction, simple fluids |
| 📦 Systems | Inventory, basic crafting, furnace, chest storage |
| 🛡️ Combat | Armor system (armor points / toughness / damage reduction), bow and arrow shooting |
| 🐷 Mobs | Basic mob spawning, creeper explosion, skeleton archery, zombie swarm attacks |
| 🌍 World | Basic chunk generation, terrain, biomes, random world seed |
| 🌐 Network | First power-on enters Setup hotspot + Web configuration; after configuration, choose to enter AP or STA mode |

---

## 🚀 Quick Start

**⚠️ When two players explore the map together, internal SRAM pressure may cause connection resets. It is recommended to enable PSRAM and reduce view distance.**

### Method 1: Download firmware directly (recommended, no compilation needed)

Download directly flashable firmware from [ESP32S3-MC Releases](https://github.com/zkd27712306/ESP32S3-MC/releases), and flash it using a flashing tool (such as [ESPWebTool](https://esptool.spacehuhn.com/)).

### Method 2: Cloud build (no local environment needed)

You can first fork this project to make compilation easier.

This project supports [GitHub Actions](https://github.com/zkd27712306/ESP32S3-MC/actions) cloud compilation, so PlatformIO does not need to be installed locally.

1. Open the repository's [Actions page](https://github.com/zkd27712306/ESP32S3-MC/actions)
2. Select **ESP32-S3 Cloud Build** on the left
3. Click **Run workflow** on the right
4. After compilation completes, download `esp32s3-firmware.zip` from the **Artifacts** section at the bottom of the run record

> ⚠️ Artifacts are valid for 30 days, so please download them promptly.

### Method 3: Local compilation

1. Open the `ESP32S3-MC-main/src/` directory with Visual Studio Code
2. Install PlatformIO; this project uses `4d_systems_esp32s3_gen4_r8n16` by default
3. Compile and flash

---

## 🔥 Flash Addresses (Bootloader 0x0)

> ⚠️ The ESP32-S3 bootloader start address is `0x0`, **not** the ESP32's `0x1000`.

When using [ESPWebTool](https://esptool.spacehuhn.com/) or esptool to flash, fill in the addresses as follows:

| File | Flash address |
|------|---------------|
| `bootloader.bin` | `0x0` |
| `partitions.bin` | `0x8000` |
| `firmware.bin` | `0x10000` |

### ESPWebTool Web Flashing

[ESPWebTool](https://esptool.spacehuhn.com/) is a browser-based ESP flashing tool that requires no software installation and can flash directly in Chrome / Edge browsers.

**Web address**: [ESPWebTool](https://esptool.spacehuhn.com/)

**Flashing steps**:

1. Open [ESPWebTool](https://esptool.spacehuhn.com/) in Chrome or Edge
2. Click the **Connect** button and select the ESP32-S3 serial port (COMx on Windows)
3. Enter the address in **Flash Address**, and select the corresponding file in **File**
4. Add the 3 files in order according to the correspondence below:

| File | Flash address |
|------|---------------|
| `bootloader.bin` | `0x0` |
| `partitions.bin` | `0x8000` |
| `firmware.bin` | `0x10000` |

5. Click **Program** to start flashing
6. Wait for the progress bar to finish; **Done** means flashing succeeded
7. Press the **RST / EN** button on the ESP32-S3 board once, or power cycle it

> 💡 **If [ESPWebTool](https://esptool.spacehuhn.com/) cannot connect to the development board, check**:
> - Whether the browser is Chrome / Edge (Firefox and Safari do not support Web Serial)
> - Whether the USB cable is a data cable (not a charge-only cable)
> - Whether the driver is installed (CH340 / CP2102 / ESP32-S3 built-in USB)
> - Whether the BOOT button is held down before clicking Connect

---

## 📡 Operation Modes

### First power-on (unconfigured)

1. Power on the ESP32-S3
2. It automatically enters **Setup configuration mode** and creates the WiFi hotspot `ESP32-MC-Setup`
3. Password: `12345678`
4. Connect a phone or computer to `ESP32-MC-Setup`
5. Open `192.168.4.1` in a browser
6. Fill in the WiFi name and password to connect, or choose between AP / STA mode
7. After saving, the ESP32 automatically restarts and enters the corresponding mode:
   - Choose **STA** → connect to your configured WiFi; the serial port prints the IP assigned by the router (such as `192.168.101.91`); use that IP + `:25565` in the Minecraft client
   - Choose **AP** → create the game hotspot `ESP32-MC`, IP `192.168.4.1`; use `192.168.4.1:25565` in the Minecraft client

> On first power-on, you must configure it once through the Web interface. After that, each power-on will run in the saved mode.

### Subsequent power-on (configured)

**AP mode (default):**

1. Power on the ESP32-S3
2. It automatically creates the game hotspot `ESP32-MC`, password `ESP32-MC`
3. The server listens on `192.168.4.1:25565`
4. Connect the Minecraft client to `192.168.4.1:25565` to play

**STA mode:**

1. Power on the ESP32-S3
2. It automatically connects to the saved WiFi
3. The serial port prints the IP assigned by the router (such as `192.168.101.91`)
4. Connect the Minecraft client to that IP + `:25565`

> To switch between AP / STA: long-press BOOT for **2 seconds**; the ESP32 will switch modes and restart.

### BOOT Button Operations

| Operation | Effect |
|-----------|--------|
| Long-press BOOT for **2 seconds** | Switch between AP / STA mode (when configured) |
| Long-press BOOT for **10 seconds** | Clear WiFi configuration; after restart, return to the first power-on Setup mode |

### Connecting Minecraft

1. Open Minecraft Java Edition **26.1.2**
2. Go to **Multiplayer → Add Server**
3. Enter the address:
   - AP mode: `192.168.4.1:25565`
   - STA mode: the IP assigned to the ESP32 by the router + `:25565`
4. Click **Join Server**

---

## ⚙️ Current Capabilities

- Player login, spawn, movement, chat
- Basic chunk generation, terrain, and biomes
- Block placement, destruction, simple fluids
- Inventory, basic crafting, furnace logic
- Basic mob spawning and some behaviors
- Armor system (armor points / toughness / damage reduction)
- Bow and arrow shooting system
- Infinite water bucket
- Chest storage
- First power-on enters Setup hotspot, configure WiFi and mode via Web
- Supports AP / STA operation modes, switchable with the BOOT button
- BOOT button resets configuration

### Default Configuration

| Parameter | Value |
|-----------|-------|
| Max players | `5` |
| View distance | `2` |
| Default port | `25565` |
| AP hotspot | `ESP32-MC` / `ESP32-MC` |
| Setup hotspot | `ESP32-MC-Setup` / `12345678` |

These values and most switch definitions are in [`ESP32S3-MC-main/src/game_types.h`](ESP32S3-MC-main/src/game_types.h).

---

## ⚠️ Known Limitations

- View distance is fixed at 2, maximum 5 players
- No redstone, no villages, no Nether, no End
- Bows and arrows have no parabola and no line-of-sight detection
- Furnace only supports some recipes
- This firmware has the Task Watchdog (Task WDT) disabled; after a crash it will not automatically restart, and RST must be pressed manually
- When two players explore the map together, internal SRAM pressure may cause connection resets. It is recommended to enable PSRAM and reduce view distance

---

## 📁 Directory Structure

The main code is all under the `ESP32S3-MC-main/src/` directory:

- [`ESP32S3-MC-main/src/code.ino`](ESP32S3-MC-main/src/code.ino): Arduino entry point; initializes serial, WiFi, Web configuration, and the main loop
- [`ESP32S3-MC-main/src/mc_server.cpp`](ESP32S3-MC-main/src/mc_server.cpp): Main server body; connection management, protocol state machine, main game logic
- [`ESP32S3-MC-main/src/mc_server.h`](ESP32S3-MC-main/src/mc_server.h): Server class header file
- [`ESP32S3-MC-main/src/packet_codec.cpp`](ESP32S3-MC-main/src/packet_codec.cpp): Minecraft packet encoding/decoding
- [`ESP32S3-MC-main/src/packet_codec.h`](ESP32S3-MC-main/src/packet_codec.h): Codec header file
- [`ESP32S3-MC-main/src/network_layer.cpp`](ESP32S3-MC-main/src/network_layer.cpp): ESP32 network layer wrapper
- [`ESP32S3-MC-main/src/network_layer.h`](ESP32S3-MC-main/src/network_layer.h): Network layer header file
- [`ESP32S3-MC-main/src/procedures.cpp`](ESP32S3-MC-main/src/procedures.cpp): Player behavior, block interaction, mob and tick-related logic
- [`ESP32S3-MC-main/src/procedures.h`](ESP32S3-MC-main/src/procedures.h): Procedure function header file
- [`ESP32S3-MC-main/src/terrain.cpp`](ESP32S3-MC-main/src/terrain.cpp): Terrain, chunk, and basic structure generation
- [`ESP32S3-MC-main/src/terrain.h`](ESP32S3-MC-main/src/terrain.h): Terrain generation header file
- [`ESP32S3-MC-main/src/crafting.cpp`](ESP32S3-MC-main/src/crafting.cpp): Crafting and furnace logic
- [`ESP32S3-MC-main/src/crafting.h`](ESP32S3-MC-main/src/crafting.h): Crafting header file
- [`ESP32S3-MC-main/src/game_state.cpp`](ESP32S3-MC-main/src/game_state.cpp): Global game state
- [`ESP32S3-MC-main/src/game_state.h`](ESP32S3-MC-main/src/game_state.h): Game state header file
- [`ESP32S3-MC-main/src/game_types.h`](ESP32S3-MC-main/src/game_types.h): Main constants, switches, and data structures
- [`ESP32S3-MC-main/src/registries.cpp`](ESP32S3-MC-main/src/registries.cpp): Protocol registries and related large data
- [`ESP32S3-MC-main/src/registries.h`](ESP32S3-MC-main/src/registries.h): Registry header file

---

## 🎮 In-Game Commands

| Command | Description |
|---------|-------------|
| `!help` | Show help |
| `!items` | List all items that can be given |
| `!give <player> <item> [amount]` | Give an item to a player |
| `!summon <mob> [amount]` | Spawn a mob |
| `!msg <player> <message>` | Private message |
| `!overworld` | Return to spawn point |

### Available Mobs

`chicken`, `cow`, `pig`, `sheep`, `zombie`, `skeleton`, `spider`, `creeper`

---

## 🛠️ Development Notes

- The current mainline code is based on `ESP32S3-MC-main/src`.
- `registries.cpp / registries.h` are large and mainly contain protocol-related static data
- Many designs in this project are intended to save resources and simplify debugging, and do not necessarily pursue the complete abstraction of common server software

---

## 🔗 Related Links

| Name | Link |
|------|------|
| GitHub repository | [zkd27712306/ESP32S3-MC](https://github.com/zkd27712306/ESP32S3-MC) |
| Actions page | [ESP32S3-MC Actions](https://github.com/zkd27712306/ESP32S3-MC/actions) |
| Releases download | [ESP32S3-MC Releases](https://github.com/zkd27712306/ESP32S3-MC/releases) |
| Original project | [ESP32-MC](https://github.com/GYGKHD/ESP32-MC) |
| ESPWebTool web flashing | [ESPWebTool](https://esptool.spacehuhn.com/) |

---

## 📄 License

This project uses the **GPL-3.0** license. See the `LICENSE` file for details.
