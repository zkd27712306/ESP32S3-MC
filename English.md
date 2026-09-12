# ESP32MC Server

> One ESP32-S3 is a Minecraft Java server.

An ultra-minimal Minecraft Java server optimized from [ESP32-MC](https://github.com/GYGKHD/ESP32-MC), running on the **ESP32-S3**. It fixes a large number of bugs from the original project (such as crashes when opening containers, empty packet sending, furnace recursion, etc.) and adds some practical features (such as the `!give` command, armor system, bow and arrow system, mob AI, etc.).

---

## 📜 Open Source License and Origin

| Item | Description |
|------|-------------|
| Original project | [ESP32-MC](https://github.com/GYGKHD/ESP32-MC) |
| Original project author | GYGKHD and all contributors who participated in code modifications |
| Original project license | GPL-3.0 |
| This project license | GPL-3.0 |
| Derivative modifications | Completed by [zkd27712306](https://github.com/zkd27712306) in 2026 |

See `LICENSE` for the full license, and `NOTICE` for the origin and modification statements.

### Main modifications in this derivative project

- Fixed crashes when opening the container interface
- Fixed incorrect entity event packet length
- Fixed empty packet sending causing zero frame length
- Fixed furnace recursion stack overflow risk
- Fixed chest storage loss / inability to open
- Fixed armor slots incorrectly participating in item stacking
- World generation: generate a random world seed on every startup
- Game features: added netherite equipment, armor system, bow and arrow system, infinite water bucket, torch placement
- Mob AI: added creeper explosion, skeleton arrow shooting, zombie group attacks
- Network mode: enters hotspot + Web configuration on first power-on, enters STA mode after configuration

---

## 🎯 Project Positioning

> A minimal Minecraft Java server running on an ESP32-S3.

**Protocol version**: `26.1.2 / 775`

The overall approach is to use as direct and traceable an implementation as possible, compressing the basic multiplayer and survival logic of Minecraft Java onto a chip with very limited resources.

### ✅ Priorities

- Run stably on the ESP32-S3
- Keep the code structure as direct as possible for continued modification
- Make problems easy to locate

### ⏸️ Not prioritized

- Complete vanilla features
- High concurrency
- Plugin compatibility
- Overly wrapped engineering structure

---

## ✨ Feature Overview

| Category | Content |
|----------|---------|
| 🎮 Gameplay | Login, spawn, movement, chat, block placement/breaking, simple fluids |
| 📦 Systems | Inventory, basic crafting, furnace, chest storage |
| 🛡️ Combat | Armor system (armor points / toughness / damage reduction), bow and arrow shooting |
| 🐷 Mobs | Basic mob spawning, creeper explosion, skeleton arrow shooting, zombie group attacks |
| 🌍 World | Basic chunk generation, terrain, biomes, random world seed |
| 🌐 Network | First power-on enters hotspot + Web configuration, then enters STA mode after configuration |

---

## 🚀 Quick Start

### Option 1: Cloud build (recommended, no local environment required)

You can fork this project first to make building easier.

This project supports [GitHub Actions](https://github.com/zkd27712306/ESP32S3-MC/actions) cloud builds, with no need to install PlatformIO locally.

1. Open the repository's [Actions page](https://github.com/zkd27712306/ESP32S3-MC/actions)
2. Select **ESP32-S3 Cloud Build** on the left
3. Click **Run workflow** on the right
4. After the build completes, download `esp32s3-firmware.zip` from the **Artifacts** section at the bottom of the run record

> ⚠️ Artifacts are valid for 30 days, so please download them promptly.

### Option 2: Local build

1. Open the `ESP32S3-MC-main/src/` directory with Visual Studio Code
2. Install PlatformIO and set the development board model in `platformio.ini` (such as `esp32-s3-devkitc-1`)
3. Compile and flash

### Option 3: Direct firmware download

Download the firmware from [ESP32S3-MC Releases](https://github.com/zkd27712306/ESP32S3-MC/releases) and flash it directly with a flashing tool (such as [ESPWebTool](https://esptool.spacehuhn.com/)).

---

## 🔥 Flashing Addresses (Bootloader 0x0)

> ⚠️ The ESP32-S3 Bootloader start address is `0x0`, **not** the ESP32's `0x1000`.

When using [ESPWebTool](https://esptool.spacehuhn.com/) or esptool to flash, fill in the addresses as follows:

| File | Flash address |
|------|---------------|
| `bootloader.bin` | `0x0` |
| `partitions.bin` | `0x8000` |
| `boot_app0.bin` | `0xe000` |
| `firmware.bin` | `0x10000` |

### ESPWebTool Web Flashing

[ESPWebTool](https://esptool.spacehuhn.com/) is a browser-based ESP flashing tool. It requires no software installation and can flash directly in Chrome / Edge browsers.

**Web address**: [ESPWebTool](https://esptool.spacehuhn.com/)

**Flashing steps**:

1. Open [ESPWebTool](https://esptool.spacehuhn.com/) with Chrome or Edge
2. Click the **Connect** button and select the ESP32-S3 serial port (COMx on Windows)
3. Enter the address in **Flash Address** and select the corresponding file in **File**
4. Add the 4 files in order according to the correspondence below:

| File | Flash address |
|------|---------------|
| `bootloader.bin` | `0x0` |
| `partitions.bin` | `0x8000` |
| `boot_app0.bin` | `0xe000` |
| `firmware.bin` | `0x10000` |

5. Click **Program** to start flashing
6. Wait for the progress bar to finish. **Done** means flashing succeeded.
7. Press the **RST / EN** button on the ESP32-S3 board, or power it on again

> 💡 **If [ESPWebTool](https://esptool.spacehuhn.com/) cannot connect to the development board, check**:
> - Whether the browser is Chrome / Edge (Firefox and Safari do not support Web Serial)
> - Whether the USB cable is a data cable (not a charge-only cable)
> - Whether the driver is installed (CH340 / CP2102 / ESP32-S3 built-in USB)
> - Whether you held the BOOT button before clicking Connect

---

## 📡 Operation Modes

### First power-on (unconfigured)

1. Power on the ESP32-S3
2. It automatically enters **AP hotspot mode** and creates a WiFi hotspot named `ESP32-MC`
3. Password: `ESP32-MC`
4. Server listening port: `25565`
5. Connect with the Minecraft Java client to `192.168.4.1:25565` and you can play directly

> The default on first power-on is AP mode, ready to use out of the box with no configuration required.

### Switching to STA mode

> ⚠️ **Only when there is no configuration file**, long-pressing BOOT for **2 seconds** will enter Setup configuration mode.

1. Long-press the BOOT button for **2 seconds**
2. After the ESP32 restarts, it enters **Setup configuration mode** and creates a hotspot named `ESP32-MC-Setup`
3. Connect to `ESP32-MC-Setup` with a phone or computer
4. Password: `12345678`
5. Open `192.168.4.1` in a browser
6. Fill in the WiFi name and password you want to connect to
7. After saving, the ESP32 automatically restarts and enters **STA mode**, connecting to your configured WiFi

> After a successful connection, the serial port will output the IP obtained by the ESP32 from the router. The Minecraft client can connect using this IP + port 25565.

### Subsequent power-on (configured)

1. Power on the ESP32-S3
2. It automatically enters **AP hotspot mode** and creates a WiFi hotspot named `ESP32-MC`
3. Password: `ESP32-MC`
4. Server listening port: `25565`
5. Long-press BOOT for **2 seconds** to enter STA mode
6. The ESP automatically connects to the saved WiFi
7. Connect your phone and computer to the WiFi and connect using the IP + port 25565

### BOOT Button Operations

| Operation | Effect |
|-----------|--------|
| Long-press BOOT for **2 seconds** | **Only when there is no configuration file**, switch to Setup configuration mode; otherwise switch between the two modes |
| Long-press BOOT for **10 seconds** | Clear the WiFi configuration; after restart, return to the default AP mode |

### Connecting to Minecraft

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
- Block placement, breaking, simple fluids
- Inventory, basic crafting, furnace logic
- Basic mob spawning and some behaviors
- Armor system (armor points / toughness / damage reduction)
- Bow and arrow shooting system
- Infinite water bucket
- Chest storage
- AP ready to use out of the box on first power-on
- Supports switching to STA mode to connect to a router
- Web configuration interface
- BOOT button mode switching and configuration reset

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

## 📁 Directory Structure

The main code is all under the `ESP32S3-MC-main/src/` directory:

- [`ESP32S3-MC-main/src/code.ino`](ESP32S3-MC-main/src/code.ino): Arduino entry point, initializes serial, WiFi, Web configuration, and the main loop
- [`ESP32S3-MC-main/src/mc_server.cpp`](ESP32S3-MC-main/src/mc_server.cpp): Server main body, connection management, protocol state machine, and main game logic
- [`ESP32S3-MC-main/src/mc_server.h`](ESP32S3-MC-main/src/mc_server.h): Server class header file
- [`ESP32S3-MC-main/src/packet_codec.cpp`](ESP32S3-MC-main/src/packet_codec.cpp): Minecraft packet encoding and decoding
- [`ESP32S3-MC-main/src/packet_codec.h`](ESP32S3-MC-main/src/packet_codec.h): Codec header file
- [`ESP32S3-MC-main/src/network_layer.cpp`](ESP32S3-MC-main/src/network_layer.cpp): ESP32 network layer wrapper
- [`ESP32S3-MC-main/src/network_layer.h`](ESP32S3-MC-main/src/network_layer.h): Network layer header file
- [`ESP32S3-MC-main/src/procedures.cpp`](ESP32S3-MC-main/src/procedures.cpp): Player behavior, block interaction, mob, and tick-related logic
- [`ESP32S3-MC-main/src/procedures.h`](ESP32S3-MC-main/src/procedures.h): Procedure function header file
- [`ESP32S3-MC-main/src/terrain.cpp`](ESP32S3-MC-main/src/terrain.cpp): Terrain, chunk, and basic structure generation
- [`ESP32S3-MC-main/src/terrain.h`](ESP32S3-MC-main/src/terrain.h): Terrain generation header file
- [`ESP32S3-MC-main/src/crafting.cpp`](ESP32S3-MC-main/src/crafting.cpp): Crafting and furnace logic
- [`ESP32S3-MC-main/src/crafting.h`](ESP32S3-MC-main/src/crafting.h): Crafting header file
- [`ESP32S3-MC-main/src/game_state.cpp`](ESP32S3-MC-main/src/game_state.cpp): Global game state
- [`ESP32S3-MC-main/src/game_state.h`](ESP32S3-MC-main/src/game_state.h): Game state header file
- [`ESP32S3-MC-main/src/game_types.h`](ESP32S3-MC-main/src/game_types.h): Main constants, switches, and data structures
- [`ESP32S3-MC-main/src/registries.cpp`](ESP32S3-MC-main/src/registries.cpp): Protocol registries and related large static data
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
| `!overworld` | Return to the spawn point |

### Available Mobs

`chicken`, `cow`, `pig`, `sheep`, `zombie`, `skeleton`, `spider`, `creeper`

---

## 🛠️ Development Notes

- The current mainline code is based on `ESP32S3-MC-main/src`.
- `registries.cpp / registries.h` are large and mainly contain protocol-related static data
- Many designs in this project are intended to save resources and simplify debugging, and do not necessarily pursue the complete abstraction of a common server

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
