<p align="center">
  <!-- logo -->
  <img src="assets/branding/05_arateki_monogram_black.svg" alt="SafraSense" width="160" />
</p>

<h1 align="center">SafraSense</h1>

<p align="center">
  <a href="README.md">Português</a> ·
  <strong>English</strong> ·
  <a href="README.es.md">Español</a> ·
  <a href="README.ja.md">日本語</a> ·
  <a href="README.zh.md">中文</a>
</p>

<p align="center">
  <em>Smart telemetry and data sovereignty for planting.<br/>
  ESP32 Firmware. Decentralized identity. Raiznet integrated.</em>
</p>

<p align="center">
  <a href="#license"><img src="https://img.shields.io/badge/license-MIT-green" alt="License" /></a>
  <img src="https://img.shields.io/badge/platform-ESP32-blue" alt="Platform" />
  <img src="https://img.shields.io/badge/status-active-brightgreen" alt="Status" />
</p>

---

**SafraSense** is a firmware solution for ESP32-based hardware, designed for agricultural monitoring and smart telemetry. It acts as the physical data collection layer in the [Arateki](https://github.com/Arateki/) ecosystem and can natively integrate with the [Raiznet](https://github.com/Arateki/raiznet) network to provide decentralized and secure data.

## 🌟 What is SafraSense?

SafraSense transforms an ESP32 into a robust sensor station for hydroponics, precision agriculture, or environmental monitoring. It not only collects data but also manages a unique and secure identity for each device, allowing small producers to share cultivation information reliably.

## 🚀 Key Features

- **Multi-Sensor Monitoring:**
  - **Air Temperature and Humidity:** Support for the DHT22 sensor.
  - **Water Level / Distance:** Precise measurement via the VL53L0X laser sensor.
  - **Nutrients (EC/TDS):** Electrical conductivity monitoring for nutrient solution control.
  - **Battery Status:** Voltage and percentage monitoring for autonomous operation.
- **Decentralized Identity:**
  - Identity generation based on **BIP-39 (12-word mnemonic)**.
  - Backup and recovery via integrated **QR Code**.
  - Derived private key for secure communications on Raiznet.
- **Interface and Connectivity:**
  - **Captive Portal (WiFiManager):** Simplified Wi-Fi setup and initial parameters.
  - **Local Dashboard:** Integrated web interface for real-time metric visualization without needing internet.
  - **Raiznet Integration:** Connectivity with local and external servers of the Arateki P2P network.
  - **Multilingual Support:** Interface available in Portuguese, English, Spanish, Japanese, and Chinese.
- **Power Management:** Sensor power pin control, optimizing battery consumption.

## 🛠️ Supported Hardware

- **Microcontroller:** ESP32 (Tested on DOIT ESP32 DEVKIT V1).
- **Sensors:**
  - DHT22 (Temperature/Humidity).
  - VL53L0X (Time-of-flight / Laser).
  - Analog TDS/Conductivity Sensor.
- **Others:** Voltage divider for battery reading, status LEDs, and physical interaction buttons.

## 💻 Technologies Used

- **Framework:** Arduino (via PlatformIO).
- **Main Libraries:**
  - `WiFiManager`: Network configuration.
  - `ArduinoJson`: Data handling.
  - `Crypto`: Cryptographic operations.
  - `QRCode` & `quirc`: Identity generation and decoding via QR.
  - `Adafruit Unified Sensor` & `DHT`: Sensor drivers.

## 📥 Installation and Configuration

This project uses **PlatformIO**. To compile and upload the firmware:

1. Install [VS Code](https://code.visualstudio.com/) and the [PlatformIO](https://platformio.org/) extension.
2. Clone this repository.
3. Open the project folder in PlatformIO.
4. Connect your ESP32 via USB.
5. Build and upload the code:
   ```bash
   pio run -t upload
   ```

## 🌐 Arateki Ecosystem

SafraSense is part of a larger ecosystem focused on technological autonomy in multiple sectors:

- **[Arateki](https://github.com/Arateki/):** Central vision and project hub.
- **[Raiznet](https://github.com/Arateki/raiznet):** P2P network for sharing cultivation data and agricultural techniques. Integration with Raiznet is **optional**, and the firmware can be used independently for local monitoring.

## 📄 License

This project is free and open-source software. See the `LICENSE` file for more details.

---
Developed by **Arateki**.
