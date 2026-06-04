<p align="center">
  <!-- logo -->
  <img src="assets/branding/05_arateki_monogram_black.svg" alt="SafraSense" width="160" />
</p>

<h1 align="center">SafraSense</h1>

<p align="center">
  <a href="README.md">Português</a> ·
  <a href="README.en.md">English</a> ·
  <a href="README.es.md">Español</a> ·
  <a href="README.ja.md">日本語</a> ·
  <strong>中文</strong>
</p>

<p align="center">
  <em>智能遥测与种植数据主权<br/>
  ESP32 固件。去中心化身份。集成 Raiznet。</em>
</p>

<p align="center">
  <a href="#license"><img src="https://img.shields.io/badge/license-MIT-green" alt="License" /></a>
  <img src="https://img.shields.io/badge/platform-ESP32-blue" alt="Platform" />
  <img src="https://img.shields.io/badge/status-active-brightgreen" alt="Status" />
</p>

---

**SafraSense** 是一款基于 ESP32 硬件的固件解决方案，专为农业监测和智能遥测而设计。它作为 [Arateki](https://github.com/Arateki/) 生态系统中的物理数据采集层，可与 [Raiznet](https://github.com/Arateki/raiznet) 网络原生集成，提供去中心化且安全的数据。

## 🌟 什么是 SafraSense？

SafraSense 将 ESP32 转换为一个强大的传感器站，适用于水培、精准农业或环境监测。它不仅采集数据，还为每台设备管理唯一且安全的身份，让小规模生产者能够可靠地分享种植信息。

## 🚀 核心功能

- **多传感器监测:**
  - **空气温湿度:** 支持 DHT22 传感器。
  - **水位 / 距离:** 通过 VL53L0X 激光传感器进行精确测量。
  - **养分 (EC/TDS):** 电导率监测，用于营养液控制。
  - **电池状态:** 电压和百分比监测，支持自主运行。
- **去中心化身份:**
  - 基于 **BIP-39（12 词助记词）** 的身份生成。
  - 通过集成的 **二维码 (QR Code)** 进行备份和恢复。
  - 用于 Raiznet 安全通信的派生私钥。
- **界面与连接性:**
  - **强制网络门户 (WiFiManager):** 简化的 Wi-Fi 设置和初始参数配置。
  - **本地控制面板:** 集成式 Web 界面，无需互联网即可实时查看监测数据。
  - **Raiznet 集成:** 连接 Arateki P2P 网络的本地和外部服务器。
  - **多语言支持:** 界面提供葡萄牙语、英语、西班牙语、日语和中文。
- **电源管理:** 传感器电源引脚控制，优化电池消耗。

## 🛠️ 支持的硬件

- **微控制器:** ESP32（已在 DOIT ESP32 DEVKIT V1 上测试）。
- **传感器:**
  - DHT22（温度/湿度）。
  - VL53L0X（飞行时间 / 激光）。
  - 模拟 TDS/电导率传感器。
- **其他:** 用于电池读取的分压器、状态 LED 和物理交互按钮。

## 💻 使用技术

- **框架:** Arduino (通过 PlatformIO)。
- **主要库:**
  - `WiFiManager`: 网络配置。
  - `ArduinoJson`: 数据处理。
  - `Crypto`: 加密操作。
  - `QRCode` & `quirc`: 通过二维码进行身份生成和解码。
  - `Adafruit Unified Sensor` & `DHT`: 传感器驱动。

## 📥 安装与配置

本项目使用 **PlatformIO**。如需编译并上传固件：

1. 安装 [VS Code](https://code.visualstudio.com/) 和 [PlatformIO](https://platformio.org/) 扩展。
2. 克隆此仓库。
3. 在 PlatformIO 中打开项目文件夹。
4. 通过 USB 连接您的 ESP32。
5. 编译并上传代码：
   ```bash
   pio run -t upload
   ```

## 🌐 Arateki 生态系统

SafraSense 是一个更大的生态系统的一部分，该生态系统专注于多个领域的数字自主：

- **[Arateki](https://github.com/Arateki/):** 核心愿景和项目枢纽。
- **[Raiznet](https://github.com/Arateki/raiznet):** 用于分享种植数据和农业技术的 P2P 网络。与 Raiznet 的集成是**可选的**，固件可独立用于本地监测。

## 📄 许可证

本项目为自由开源软件。详见 `LICENSE` 文件。

---
Developed by **Arateki**.
