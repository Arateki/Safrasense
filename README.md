<p align="center">
  <!-- logo -->
  <img src="assets/branding/05_arateki_monogram_black.svg" alt="SafraSense" width="160" />
</p>

<h1 align="center">SafraSense</h1>

<p align="center">
  <strong>Português</strong> ·
  <a href="README.en.md">English</a> ·
  <a href="README.es.md">Español</a> ·
  <a href="README.ja.md">日本語</a> ·
  <a href="README.zh.md">中文</a>
</p>

<p align="center">
  <em>Telemetria inteligente e soberania de dados para o plantio.<br/>
  Firmware ESP32. Identidade descentralizada. Integrado à Raiznet.</em>
</p>

<p align="center">
  <a href="#licença"><img src="https://img.shields.io/badge/license-MIT-green" alt="License" /></a>
  <img src="https://img.shields.io/badge/platform-ESP32-blue" alt="Platform" />
  <img src="https://img.shields.io/badge/status-active-brightgreen" alt="Status" />
</p>

---

O **SafraSense** é uma solução de firmware para hardware baseado em ESP32, projetado para monitoramento agrícola e telemetria inteligente. Ele atua como a camada física de coleta de dados no ecossistema [Arateki](https://github.com/Arateki/), podendo se integrar nativamente com a rede [Raiznet](https://github.com/Arateki/raiznet) para fornecer dados descentralizados e seguros.

## 🌟 O que é o SafraSense?

O SafraSense transforma um ESP32 em uma estação de sensores robusta para hidroponia, agricultura de precisão ou monitoramento ambiental. Ele não apenas coleta dados, mas também gerencia uma identidade única e segura para cada dispositivo, permitindo que pequenos produtores compartilhem informações de cultivo de forma confiável.

## 🚀 Principais Funcionalidades

- **Monitoramento Multissensorial:**
  - **Temperatura e Umidade do Ar:** Suporte ao sensor DHT22.
  - **Nível de Água / Distância:** Medição precisa via sensor laser VL53L0X.
  - **Nutrientes (EC/TDS):** Monitoramento da condutividade elétrica para controle de soluções nutritivas.
  - **Status da Bateria:** Monitoramento de voltagem e porcentagem para operação autônoma.
- **Identidade Descentralizada:**
  - Geração de identidade baseada em **BIP-39 (Mnemonic de 12 palavras)**.
  - Backup e recuperação via **QR Code** integrado.
  - Chave privada derivada para comunicações seguras na Raiznet.
- **Interface e Conectividade:**
  - **Portal Cativo (WiFiManager):** Configuração simplificada de Wi-Fi e parâmetros iniciais.
  - **Dashboard Local:** Interface web integrada para visualização em tempo real das métricas sem necessidade de internet.
  - **Integração Raiznet:** Conectividade com servidores locais e externos da rede P2P Arateki.
  - **Suporte Multilíngue:** Interface disponível em Português, Inglês, Espanhol, Japonês e Chinês.
- **Gestão de Energia:** Controle de pinos de energia para sensores, otimizando o consumo de bateria.

## 🛠️ Hardware Suportado

- **Microcontrolador:** ESP32 (Testado em DOIT ESP32 DEVKIT V1).
- **Sensores:**
  - DHT22 (Temperatura/Umidade).
  - VL53L0X (Tempo de voo / Laser).
  - Sensor de TDS/Condutividade Analógico.
- **Outros:** Divisor de tensão para leitura de bateria, LEDs de status e botões de interação física.

## 💻 Tecnologias Utilizadas

- **Framework:** Arduino (via PlatformIO).
- **Bibliotecas Principais:**
  - `WiFiManager`: Configuração de rede.
  - `ArduinoJson`: Manipulação de dados.
  - `Crypto`: Operações criptográficas.
  - `QRCode` & `quirc`: Geração e decodificação de identidades via QR.
  - `Adafruit Unified Sensor` & `DHT`: Driver de sensores.

## 📥 Instalação e Configuração

Este projeto utiliza o **PlatformIO**. Para compilar e carregar o firmware:

1. Instale o [VS Code](https://code.visualstudio.com/) e a extensão [PlatformIO](https://platformio.org/).
2. Clone este repositório.
3. Abra a pasta do projeto no PlatformIO.
4. Conecte seu ESP32 via USB.
5. Compile e carregue o código:
   ```bash
   pio run -t upload
   ```

## 🌐 Ecossistema Arateki

O SafraSense é parte de um ecossistema maior focado em autonomia tecnológica em múltiplos setores:

- **[Arateki](https://github.com/Arateki/):** Visão central e hub de projetos.
- **[Raiznet](https://github.com/Arateki/raiznet):** Rede P2P para compartilhamento de dados de cultivo e técnicas agrícolas. A integração com a Raiznet é **opcional**, e o firmware pode ser utilizado de forma independente para monitoramento local.

## 📄 Licença

Este projeto é software livre e de código aberto. Consulte o arquivo `LICENSE` para mais detalhes.

---
Desenvolvido por **Arateki**.
