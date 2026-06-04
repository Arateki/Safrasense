<p align="center">
  <!-- logo -->
  <img src="assets/branding/05_arateki_monogram_black.svg" alt="SafraSense" width="160" />
</p>

<h1 align="center">SafraSense</h1>

<p align="center">
  <a href="README.md">Português</a> ·
  <a href="README.en.md">English</a> ·
  <strong>Español</strong> ·
  <a href="README.ja.md">日本語</a> ·
  <a href="README.zh.md">中文</a>
</p>

<p align="center">
  <em>Telemetría inteligente y soberanía de datos para el plantío.<br/>
  Firmware ESP32. Identidad descentralizada. Integrado a Raiznet.</em>
</p>

<p align="center">
  <a href="#licencia"><img src="https://img.shields.io/badge/license-MIT-green" alt="License" /></a>
  <img src="https://img.shields.io/badge/platform-ESP32-blue" alt="Platform" />
  <img src="https://img.shields.io/badge/status-active-brightgreen" alt="Status" />
</p>

---

**SafraSense** es una solución de firmware para hardware basado en ESP32, diseñado para el monitoreo agrícola y telemetría inteligente. Actúa como la capa física de recolección de datos en el ecosistema [Arateki](https://github.com/Arateki/) y puede integrarse nativamente con la red [Raiznet](https://github.com/Arateki/raiznet) para proporcionar datos descentralizados y seguros.

## 🌟 ¿Qué es SafraSense?

SafraSense transforma un ESP32 en una estación de sensores robusta para hidroponía, agricultura de precisión o monitoreo ambiental. No solo recolecta datos, sino que también gestiona una identidad única y segura para cada dispositivo, permitiendo que los pequeños productores compartan información de cultivo de forma confiable.

## 🚀 Funcionalidades Principales

- **Monitoreo Multisensorial:**
  - **Temperatura y Humedad del Aire:** Soporte para el sensor DHT22.
  - **Nivel de Agua / Distancia:** Medición precisa a través del sensor láser VL53L0X.
  - **Nutrientes (EC/TDS):** Monitoreo de la conductividad eléctrica para el control de soluciones nutritivas.
  - **Estado de la Batería:** Monitoreo de voltaje y porcentaje para operación autónoma.
- **Identidad Descentralizada:**
  - Generación de identidad basada en **BIP-39 (Mnemónico de 12 palabras)**.
  - Respaldo y recuperación a través de **Código QR** integrado.
  - Clave privada derivada para comunicaciones seguras en Raiznet.
- **Interfaz y Conectividad:**
  - **Portal Cautivo (WiFiManager):** Configuración simplificada de Wi-Fi y parámetros iniciales.
  - **Dashboard Local:** Interfaz web integrada para la visualización en tiempo real de métricas sin necesidad de internet.
  - **Integración Raiznet:** Conectividad con servidores locales y externos de la red P2P Arateki.
  - **Soporte Multilingüe:** Interfaz disponible en Portugués, Inglés, Español, Japonés y Chino.
- **Gestión de Energía:** Control de pines de energía para sensores, optimizando el consumo de batería.

## 🛠️ Hardware Soportado

- **Microcontrolador:** ESP32 (Probado en DOIT ESP32 DEVKIT V1).
- **Sensores:**
  - DHT22 (Temperatura/Humedad).
  - VL53L0X (Tiempo de vuelo / Láser).
  - Sensor de TDS/Conductividad Analógico.
- **Otros:** Divisor de voltaje para lectura de batería, LEDs de estado y botones de interacción física.

## 💻 Tecnologías Utilizadas

- **Framework:** Arduino (vía PlatformIO).
- **Bibliotecas Principales:**
  - `WiFiManager`: Configuración de red.
  - `ArduinoJson`: Manipulación de datos.
  - `Crypto`: Operaciones criptográficas.
  - `QRCode` & `quirc`: Generación y decodificación de identidades vía QR.
  - `Adafruit Unified Sensor` & `DHT`: Drivers de sensores.

## 📥 Instalación y Configuración

Este proyecto utiliza **PlatformIO**. Para compilar y cargar el firmware:

1. Instale [VS Code](https://code.visualstudio.com/) y la extensión [PlatformIO](https://platformio.org/).
2. Clone este repositorio.
3. Abra la carpeta del proyecto en PlatformIO.
4. Conecte su ESP32 vía USB.
5. Compile y cargue el código:
   ```bash
   pio run -t upload
   ```

## 🌐 Ecosistema Arateki

SafraSense es parte de un ecosistema más amplio enfocado en la autonomía tecnológica en múltiples sectores:

- **[Arateki](https://github.com/Arateki/):** Visión central y hub de proyectos.
- **[Raiznet](https://github.com/Arateki/raiznet):** Red P2P para compartir datos de cultivo y técnicas agrícolas. La integración con Raiznet es **opcional**, y el firmware puede ser utilizado de forma independiente para el monitoreo local.

## 📄 Licencia

Este proyecto es software libre y de código abierto. Consulte el archivo `LICENSE` para más detalles.

---
Desarrollado por **Arateki**.
