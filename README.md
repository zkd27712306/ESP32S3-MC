# ESP32MC Server

在[ESP32-MC](https://github.com/GYGKHD/ESP32-MC)基础上进行优化，解决很多bug（合成时崩溃等），新增部分功能（!give命令）。

## 开源许可与来源
- 原项目作者：GYGKHD及所有参与代码修改的贡献者
- 原项目许可证：GPL-3.0
- 本项目许可证：GPL-3.0
- 派生项目新增与修改部分由 [zkd27712306] 于 2026 年完成
- 完整许可证见 LICENSE，来源和修改声明见 NOTICE。

本派生项目主要修改包括：
- 修复打开容器界面崩溃问题
- 修复实体事件包长度错误
- 修复空包发送导致帧长度为零
- 修复熔炉递归栈溢出风险
- 世界生成：每次启动生成随机世界种子
- 游戏功能：新增下界合金装备、护甲系统、弓箭系统、无限水桶、火把放置
- 生物 AI：新增苦力怕爆炸、骷髅射箭、僵尸群攻
- 网络模式：纯AP热点模式，开箱即用

## 项目定位

一个跑在 ESP32S3 上的极简 Minecraft Java 服务器。

这个项目目前主要面向 Arduino ESP32S3 环境，协议版本是 `26.1.2 / 775`。整体思路是尽量用直接、可追踪的实现，把 Minecraft Java 的基础联机和生存逻辑压到一块资源很紧的芯片上。

它像一个能在 ESP32 上自己跑起来的实验性小型生存服，优先考虑的是：

- 在 ESP32S3 上能稳定跑起来
- 代码结构尽量直接，方便继续改
- 出问题时容易定位

暂时不优先考虑的是：

- 完整原版特性
- 高并发
- 插件兼容
- 过度包装的工程结构

### 云编译（GitHub Actions）

本项目支持手动触发 [GitHub Actions](https://github.com/zkd27712306/ESP32S3-MC/actions) 云端编译，无需本地安装 PlatformIO。

1. 打开仓库的 [Actions 页面](https://github.com/zkd27712306/ESP32S3-MC/actions)
2. 左侧选择 **ESP32-S3 云构建**
3. 右侧点击 **Run workflow**
4. 编译完成后，在运行记录底部的 **Artifacts** 区域下载 `esp32s3-firmware.zip`，如https://github.com/zkd27712306/ESP32S3-MC/actions/runs/*/artifacts/*

## 当前能力

现在已经有的内容包括：

- 玩家登录、出生、移动、聊天
- 基础区块生成、地形和生物群系
- 方块放置、破坏、简单流体
- 背包、基础合成、熔炉逻辑
- 基础Mob刷新和部分行为
- 护甲系统（护甲值/韧性/减伤）
- 弓箭射击系统
- 无限水桶
- AP热点模式，开箱即用

当前默认配置：

- 最大玩家数：`5`
- 视距：`2`
- 默认端口：`25565`

这些值和多数开关定义都在 [`ESP32S3-MC-main/src/game_types.h`](ESP32S3-MC-main/src/game_types.h)。

## 运行方式

### 在 ESP32S3 上运行

默认入口是 [`ESP32S3-MC-main/src/code.ino`](ESP32S3-MC-main/src/code.ino)。

大致流程：

1. 用 Visual Studio Code 打开 ESP32S3-MC-main/src/ 目录
2. 安装 PlatformIO，并在 platformio.ini 中设置开发板型号（如 esp32-s3-devkitc-1）
3. 编译并烧录
4. 设备启动后会打开一个名为 ESP32-MC 的 WiFi
5. 服务器开始监听 `25565`
6. Minecraft Java 客户端连接到 `192.168.4.1:25565` 即可

启动时串口会输出网络状态、IP 地址和启动信息，方便排查。

或者直接下载 [ESP32S3-MC Releases](https://github.com/zkd27712306/ESP32S3-MC/releases)，并使用烧录工具（如 ESPWebTool）把程序直接烧录到你的开发板。

### WiFi 连接

设备启动后会在 AP 模式下创建名为 `ESP32-MC` 的 WiFi 热点，密码为 `ESP32-MC`。

## 目录结构

当前主要代码都在 `ESP32S3-MC-main/src/` 目录下：

- [`ESP32S3-MC-main/src/code.ino`](ESP32S3-MC-main/src/code.ino)：Arduino 入口，初始化串口、WiFi 和主循环
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

## 开发说明

- 当前主线代码以 `ESP32S3-MC-main/src` 为准。
- `registries.cpp / registries.h` 体积较大，主要是协议相关的静态数据
- 这个项目的很多设计是为了节省资源和简化调试，不一定追求常见服务端那种完整抽象
