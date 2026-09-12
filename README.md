# ESP32MC Server

English version:[English](https://github.com/zkd27712306/ESP32S3-MC/blob/main/English.md)

> 一块 ESP32-S3，就是一个 Minecraft Java 服务器。

基于 [ESP32-MC](https://github.com/GYGKHD/ESP32-MC) 优化而来的极简 Minecraft Java 服务器，运行在 **ESP32-S3** 上。修复了大量原有 Bug（如打开容器崩溃、空包发送、熔炉递归等），并新增了部分实用功能（如 `!give` 指令、护甲系统、弓箭系统、生物 AI 等）。

---

## 📜 开源许可与来源

| 项目 | 说明 |
|------|------|
| 原项目 | [ESP32-MC](https://github.com/GYGKHD/ESP32-MC) |
| 原项目作者 | GYGKHD 及所有参与代码修改的贡献者 |
| 原项目许可证 | GPL-3.0 |
| 本项目许可证 | GPL-3.0 |
| 派生修改 | 由 [zkd27712306](https://github.com/zkd27712306) 于 2026 年完成 |

完整许可证见 `LICENSE`，来源和修改声明见 `NOTICE`。

### 本派生项目主要修改

- 修复打开容器界面崩溃问题
- 修复实体事件包长度错误
- 修复空包发送导致帧长度为零
- 修复熔炉递归栈溢出风险
- 修复箱子存储丢失 / 无法打开问题
- 修复护甲槽错误参与物品堆叠
- 世界生成：每次启动生成随机世界种子
- 游戏功能：新增下界合金装备、护甲系统、弓箭系统、无限水桶、火把放置
- 生物 AI：新增苦力怕爆炸、骷髅射箭、僵尸群攻
- 网络模式：首次上电进入热点 + Web 配置，配置后进入 STA 模式

---

## 🎯 项目定位

> 一个跑在 ESP32-S3 上的极简 Minecraft Java 服务器。

**协议版本**：`26.1.2 / 775`

整体思路是尽量用直接、可追踪的实现，把 Minecraft Java 的基础联机和生存逻辑压到一块资源很紧的芯片上。

### ✅ 优先考虑

- 在 ESP32-S3 上能稳定跑起来
- 代码结构尽量直接，方便继续改
- 出问题时容易定位

### ⏸️ 暂不优先

- 完整原版特性
- 高并发
- 插件兼容
- 过度包装的工程结构

---

## ✨ 特性一览

| 类别 | 内容 |
|------|------|
| 🎮 玩法 | 登录、出生、移动、聊天、方块放置/破坏、简单流体 |
| 📦 系统 | 背包、基础合成、熔炉、箱子存储 |
| 🛡️ 战斗 | 护甲系统（护甲值 / 韧性 / 减伤）、弓箭射击 |
| 🐷 生物 | 基础 Mob 刷新、苦力怕爆炸、骷髅射箭、僵尸群攻 |
| 🌍 世界 | 基础区块生成、地形、生物群系、随机世界种子 |
| 🌐 网络 | 首次上电进热点 + Web 配置，配置后进入 STA 模式 |

---

## 🚀 快速开始

### 方式一：云编译（推荐，无需本地环境）

你可以先fork本项目，以便更好编译。

本项目支持 [GitHub Actions](https://github.com/zkd27712306/ESP32S3-MC/actions) 云端编译，无需本地安装 PlatformIO。

1. 打开仓库的 [Actions 页面](https://github.com/zkd27712306/ESP32S3-MC/actions)
2. 左侧选择 **ESP32-S3 云构建**
3. 右侧点击 **Run workflow**
4. 编译完成后，在运行记录底部的 **Artifacts** 区域下载 `esp32s3-firmware.zip`

> ⚠️ Artifact 有效期 30 天，请及时下载。

### 方式二：本地编译

1. 用 Visual Studio Code 打开 `ESP32S3-MC-main/src/` 目录
2. 安装 PlatformIO，并在 `platformio.ini` 中设置开发板型号（如 `esp32-s3-devkitc-1`）
3. 编译并烧录

### 方式三：直接下载固件

从 [ESP32S3-MC Releases](https://github.com/zkd27712306/ESP32S3-MC/releases) 下载固件，使用烧录工具（如 [ESPWebTool](https://esptool.spacehuhn.com/)）直接烧录。

---

## 🔥 烧录地址（Bootloader 0x0）

> ⚠️ ESP32-S3 的 Bootloader 起始地址是 `0x0`，**不是** ESP32 的 `0x1000`。

使用 [ESPWebTool](https://esptool.spacehuhn.com/) 或 esptool 烧录时，请按以下地址填写：

| 文件 | 烧录地址 |
|------|----------|
| `bootloader.bin` | `0x0` |
| `partitions.bin` | `0x8000` |
| `boot_app0.bin` | `0xe000` |
| `firmware.bin` | `0x10000` |

### ESPWebTool 网页烧录

[ESPWebTool](https://esptool.spacehuhn.com/) 是一个基于浏览器的 ESP 烧录工具，无需安装任何软件，直接在 Chrome / Edge 浏览器里烧录。

**网页地址**：[ESPWebTool](https://esptool.spacehuhn.com/)

**烧录步骤**：

1. 用 Chrome 或 Edge 打开 [ESPWebTool](https://esptool.spacehuhn.com/)
2. 点击 **Connect** 按钮，选择 ESP32-S3 的串口（Windows 下是 COMx）
3. 在 **Flash Address** 处填写地址，**File** 处选择对应文件
4. 按下面的对应关系依次添加 4 个文件：

| 文件 | 烧录地址 |
|------|----------|
| `bootloader.bin` | `0x0` |
| `partitions.bin` | `0x8000` |
| `boot_app0.bin` | `0xe000` |
| `firmware.bin` | `0x10000` |

5. 点击 **Program** 开始烧录
6. 等待进度条走完，显示 **Done** 即烧录成功
7. 按一下 ESP32-S3 板上的 **RST / EN** 键，或重新上电

> 💡 **如果 [ESPWebTool](https://esptool.spacehuhn.com/) 连不上开发板，检查**：
> - 浏览器是否为 Chrome / Edge（Firefox、Safari 不支持 Web Serial）
> - USB 线是否是数据线（不是纯充电线）
> - 驱动是否安装（CH340 / CP2102 / ESP32-S3 内置 USB）
> - 是否按住了 BOOT 键再点 Connect

---

## 📡 运行方式

### 首次上电（未配置）

1. ESP32-S3 上电
2. 自动进入 **AP 热点模式**，创建名为 `ESP32-MC` 的 WiFi 热点
3. 密码：`ESP32-MC`
4. 服务器监听端口：`25565`
5. Minecraft Java 客户端连接 `192.168.4.1:25565` 即可直接游玩

> 首次上电默认 AP 模式，即开即用，无需任何配置。

### 切换到 STA 模式

> ⚠️ **仅在没有配置文件的情况下**，长按 BOOT **2 秒** 才会进入 Setup 配置模式。

1. 长按 BOOT 键 **2 秒**
2. ESP32 重启后进入 **Setup 配置模式**，创建名为 `ESP32-MC-Setup` 的热点
3. 用手机或电脑连上 `ESP32-MC-Setup`
4. 密码：`12345678`
5. 浏览器打开 `192.168.4.1`
6. 填写要连接的 WiFi 名称和密码
7. 保存后 ESP32 自动重启，进入 **STA 模式**，连接你配置的 WiFi

> 连接成功后，串口会输出 ESP32 从路由器获取到的 IP，Minecraft 客户端用这个 IP +25565端口连接即可。

### 后续上电（已配置）

1. ESP32-S3 上电
2. 自动进入 **AP 热点模式**，创建名为 `ESP32-MC` 的 WiFi 热点
3. 密码：`ESP32-MC`
4. 服务器监听端口：`25565`
5. 长按 BOOT **2 秒** 进入STA模式
6. ESP自动连接已保存的WIFI
7. 手机和电脑连接WIFI，用 IP +25565端口连接即可

### BOOT 键操作

| 操作 | 效果 |
|------|------|
| 长按 BOOT **2 秒** | **仅在没有配置文件时**，切换到 Setup 配置模式 ，否则在两种模式间切换 |
| 长按 BOOT **10 秒** | 清空 WiFi 配置，重启后回到默认 AP 模式 |

### 连接 Minecraft

1. 打开 Minecraft Java 版 **26.1.2**
2. 进入 **多人游戏 → 添加服务器**
3. 地址填写：
   - AP 模式：`192.168.4.1:25565`
   - STA 模式：路由器分配给 ESP32 的 IP + `:25565`
4. 点击 **加入服务器**

---

## ⚙️ 当前能力

- 玩家登录、出生、移动、聊天
- 基础区块生成、地形和生物群系
- 方块放置、破坏、简单流体
- 背包、基础合成、熔炉逻辑
- 基础 Mob 刷新和部分行为
- 护甲系统（护甲值 / 韧性 / 减伤）
- 弓箭射击系统
- 无限水桶
- 箱子存储
- 首次上电 AP 即开即用
- 支持切换到 STA 模式连接路由器
- Web 配置界面
- BOOT 键切换模式、重置配置

### 默认配置

| 参数 | 值 |
|------|-----|
| 最大玩家数 | `5` |
| 视距 | `2` |
| 默认端口 | `25565` |
| AP 热点 | `ESP32-MC` / `ESP32-MC` |
| Setup 热点 | `ESP32-MC-Setup` / `12345678` |

这些值和多数开关定义都在 [`ESP32S3-MC-main/src/game_types.h`](ESP32S3-MC-main/src/game_types.h)。

---

## 📁 目录结构

主要代码都在 `ESP32S3-MC-main/src/` 目录下：

- [`ESP32S3-MC-main/src/code.ino`](ESP32S3-MC-main/src/code.ino)：Arduino 入口，初始化串口、WiFi、Web 配置和主循环
- [`ESP32S3-MC-main/src/mc_server.cpp`](ESP32S3-MC-main/src/mc_server.cpp)：服务器主体，连接管理、协议状态机、主要游戏逻辑
- [`ESP32S3-MC-main/src/mc_server.h`](ESP32S3-MC-main/src/mc_server.h)：服务器类头文件
- [`ESP32S3-MC-main/src/packet_codec.cpp`](ESP32S3-MC-main/src/packet_codec.cpp)：Minecraft 数据包编解码
- [`ESP32S3-MC-main/src/packet_codec.h`](ESP32S3-MC-main/src/packet_codec.h)：编解码器头文件
- [`ESP32S3-MC-main/src/network_layer.cpp`](ESP32S3-MC-main/src/network_layer.cpp)：ESP32 网络层封装
- [`ESP32S3-MC-main/src/network_layer.h`](ESP32S3-MC-main/src/network_layer.h)：网络层头文件
- [`ESP32S3-MC-main/src/procedures.cpp`](ESP32S3-MC-main/src/procedures.cpp)：玩家行为、方块交互、Mob 和 Tick 相关逻辑
- [`ESP32S3-MC-main/src/procedures.h`](ESP32S3-MC-main/src/procedures.h)：过程函数头文件
- [`ESP32S3-MC-main/src/terrain.cpp`](ESP32S3-MC-main/src/terrain.cpp)：地形、区块和基础结构生成
- [`ESP32S3-MC-main/src/terrain.h`](ESP32S3-MC-main/src/terrain.h)：地形生成头文件
- [`ESP32S3-MC-main/src/crafting.cpp`](ESP32S3-MC-main/src/crafting.cpp)：合成和熔炉逻辑
- [`ESP32S3-MC-main/src/crafting.h`](ESP32S3-MC-main/src/crafting.h)：合成头文件
- [`ESP32S3-MC-main/src/game_state.cpp`](ESP32S3-MC-main/src/game_state.cpp)：全局游戏状态
- [`ESP32S3-MC-main/src/game_state.h`](ESP32S3-MC-main/src/game_state.h)：游戏状态头文件
- [`ESP32S3-MC-main/src/game_types.h`](ESP32S3-MC-main/src/game_types.h)：主要常量、开关和数据结构
- [`ESP32S3-MC-main/src/registries.cpp`](ESP32S3-MC-main/src/registries.cpp)：协议注册表和相关大体积数据
- [`ESP32S3-MC-main/src/registries.h`](ESP32S3-MC-main/src/registries.h)：注册表头文件

---

## 🎮 游戏内指令

| 指令 | 说明 |
|------|------|
| `!help` | 显示帮助 |
| `!items` | 列出所有可给予的物品 |
| `!give <玩家> <物品> [数量]` | 给玩家物品 |
| `!summon <生物> [数量]` | 生成生物 |
| `!msg <玩家> <消息>` | 私聊 |
| `!overworld` | 回到出生点 |

### 可用生物

`chicken`、`cow`、`pig`、`sheep`、`zombie`、`skeleton`、`spider`、`creeper`

---

## 🛠️ 开发说明

- 当前主线代码以 `ESP32S3-MC-main/src` 为准。
- `registries.cpp / registries.h` 体积较大，主要是协议相关的静态数据
- 这个项目的很多设计是为了节省资源和简化调试，不一定追求常见服务端那种完整抽象

---

## 🔗 相关链接

| 名称 | 链接 |
|------|------|
| GitHub 仓库 | [zkd27712306/ESP32S3-MC](https://github.com/zkd27712306/ESP32S3-MC) |
| Actions 页面 | [ESP32S3-MC Actions](https://github.com/zkd27712306/ESP32S3-MC/actions) |
| Releases 下载 | [ESP32S3-MC Releases](https://github.com/zkd27712306/ESP32S3-MC/releases) |
| 原项目 | [ESP32-MC](https://github.com/GYGKHD/ESP32-MC) |
| ESPWebTool 网页烧录 | [ESPWebTool](https://esptool.spacehuhn.com/) |

---

## 📄 许可证

本项目采用 **GPL-3.0** 许可证，详见 `LICENSE` 文件。
