<p align="center">
  <!-- logo -->
  <img src="assets/branding/05_arateki_monogram_black.svg" alt="SafraSense" width="160" />
</p>

<h1 align="center">SafraSense</h1>

<p align="center">
  <a href="README.md">Português</a> ·
  <a href="README.en.md">English</a> ·
  <a href="README.es.md">Español</a> ·
  <strong>日本語</strong> ·
  <a href="README.zh.md">中文</a>
</p>

<p align="center">
  <em>スマートなテレメトリと栽培のためのデータ主権<br/>
  ESP32 ファームウェア。分散型アイデンティティ。Raiznet 統合。</em>
</p>

<p align="center">
  <a href="#license"><img src="https://img.shields.io/badge/license-MIT-green" alt="License" /></a>
  <img src="https://img.shields.io/badge/platform-ESP32-blue" alt="Platform" />
  <img src="https://img.shields.io/badge/status-active-brightgreen" alt="Status" />
</p>

---

**SafraSense** は、ESP32ベースのハードウェア向けのファームウェアソリューションであり、農業モニタリングとスマートなテレメトリのために設計されています。これは [Arateki](https://github.com/Arateki/) エコシステムにおける物理的なデータ収集レイヤーとして機能し、[Raiznet](https://github.com/Arateki/raiznet) ネットワークとネイティブに統合して、分散型で安全なデータを提供することができます。

## 🌟 SafraSense とは？

SafraSense は、ESP32を水耕栽培、精密農業、または環境モニタリング用の堅牢なセンサーステーションに変えます。データを収集するだけでなく、各デバイスのユニークで安全なアイデンティティを管理し、小規模生産者が栽培情報を信頼性高く共有できるようにします。

## 🚀 主な機能

- **マルチセンサーモニタリング:**
  - **空気の温度と湿度:** DHT22 センサーをサポート。
  - **水位 / 距離:** VL53L0X レーザーセンサーによる精密測定。
  - **養分 (EC/TDS):** 培養液管理のための電気伝導度モニタリング。
  - **バッテリーステータス:** 自律運用のための電圧とパーセンテージのモニタリング。
- **分散型アイデンティティ:**
  - **BIP-39 (12単語のニーモニック)** に基づくアイデンティティ生成。
  - 統合された **QRコード** によるバックアップと復元。
  - Raiznet 上の安全な通信のための派生プライベートキー。
- **インターフェースと接続性:**
  - **キャプティブポータル (WiFiManager):** Wi-Fi 設定と初期パラメータの簡素化。
  - **ローカルダッシュボード:** インターネットなしでメトリクスをリアルタイムに視覚化する統合ウェブインターフェース。
  - **Raiznet 統合:** Arateki P2P ネットワークのローカルおよび外部サーバーへの接続。
  - **多言語サポート:** ポルトガル語、英語、スペイン語、日本語、中国語で利用可能なインターフェース。
- **電力管理:** センサー用電源ピンの制御により、バッテリー消費を最適化。

## 🛠️ サポートされているハードウェア

- **マイコン:** ESP32 (DOIT ESP32 DEVKIT V1 でテスト済み)。
- **センサー:**
  - DHT22 (温度/湿度)。
  - VL53L0X (タイム・オブ・フライト / レーザー)。
  - アナログ TDS/伝導率センサー。
- **その他:** バッテリー読み取り用の分圧器、ステータス LED、物理的な操作ボタン。

## 💻 使用テクノロジー

- **フレームワーク:** Arduino (PlatformIO 経由)。
- **主なライブラリ:**
  - `WiFiManager`: ネットワーク設定。
  - `ArduinoJson`: データ処理。
  - `Crypto`: 暗号化操作。
  - `QRCode` & `quirc`: QR によるアイデンティティ生成とデコード。
  - `Adafruit Unified Sensor` & `DHT`: センサードライバー。

## 📥 インストールと設定

このプロジェクトは **PlatformIO** を使用しています。ファームウェアをコンパイルしてアップロードするには：

1. [VS Code](https://code.visualstudio.com/) と [PlatformIO](https://platformio.org/) 拡張機能をインストールします。
2. このリポジトリをクローンします。
3. PlatformIO でプロジェクトフォルダを開きます。
4. USB 経由で ESP32 を接続します。
5. コードをビルドしてアップロードします：
   ```bash
   pio run -t upload
   ```

## 🌐 Arateki エコシステム

SafraSense は、複数の分野における技術的自立に焦点を当てた、より大きなエコシステムの一部です：

- **[Arateki](https://github.com/Arateki/):** 中心的なビジョンとプロジェクトハブ。
- **[Raiznet](https://github.com/Arateki/raiznet):** 栽培データと農業技術を共有するための P2P ネットワーク。Raiznet との統合は **オプション** であり、ファームウェアはローカルモニタリングのために独立して使用できます。

## 📄 ライセンス

このプロジェクトはフリーでオープンソースのソフトウェアです。詳細は `LICENSE` ファイルを参照してください。

---
Developed by **Arateki**.
