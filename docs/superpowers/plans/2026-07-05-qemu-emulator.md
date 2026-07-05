# Emulador QEMU local (pivô do Wokwi) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rodar o firmware completo num ESP32 emulado **localmente** (QEMU da Espressif, headless), falando com o `raiznetd` via `10.0.2.2`, com o e2e das duas fases executando de verdade.

**Architecture:** Env `qemu` com build híbrido `arduino, espidf` habilita o driver Ethernet virtual `openeth`. Sob `-DQEMU_EMULATOR` a rede sobe por `esp_eth` em vez de Wi-Fi; um helper `netReady()` substitui os checks `WiFi.status()` em produção e emulador. Os artefatos Wokwi são substituídos: os scripts `emu/run.sh` e `emu/e2e.sh` passam a orquestrar QEMU (imagem de flash via `esptool merge_bin`, rede slirp com `hostfwd`).

**Tech Stack:** PlatformIO espressif32 (arduino+espidf híbrido, core 2.0.17/IDF 4.4), QEMU fork Espressif (`qemu-system-xtensa`, machine esp32), esptool, bash+curl+jq, raiznetd.

## Global Constraints

- Env de produção `esp32doit-devkit-v1` intocado; todo código de emulador atrás de `#ifdef QEMU_EMULATOR`.
- Servidores do emulador: externo `http://10.0.2.2:3000/v1/telemetry`, local `10.0.2.2:3001`. NUNCA `raiznet.com`.
- raiznetd: worktree `../Raiznet-rust`, binário direto (`cargo build` + `target/debug/raiznetd`), `RAIZNET_DATA_DIR` absoluto.
- `"Device not found"` no body do 207 é o gatilho do re-registro (fix já commitado em `1f170c5`).
- PATH das ferramentas: `pio` em `$HOME/.platformio/penv/bin`; `esptool.py` em `$HOME/.platformio/packages/tool-esptoolpy/`.
- QEMU Espressif: release pinada `esp-develop-9.0.0-20240606` de https://github.com/espressif/qemu/releases, extraída em `emu/.qemu/` (git-ignorada). Não usar o qemu-system-xtensa da distro (não tem a machine esp32).
- Mensagens de commit sem menção a Claude e sem trailer Co-Authored-By.

---

### Task 1: Env híbrido `qemu` compilando com openeth

**Files:**
- Modify: `platformio.ini`
- Create: `sdkconfig.defaults` (raiz do projeto — o build híbrido exige; contém só o necessário)
- Possibly create: `CMakeLists.txt` na raiz e `src/CMakeLists.txt` (o PIO gera se faltarem; criar apenas se o build exigir)

**Interfaces:**
- Produces: `pio run -e qemu` SUCCESS com `esp_eth_mac_new_openeth` linkável e `-DQEMU_EMULATOR` definido. Tasks 2–5 dependem disso.

Este é o passo de maior risco (builds híbridos são sensíveis). O critério de pronto é objetivo: compila e o símbolo do openeth existe na libesp_eth compilada.

- [ ] **Step 1: Substituir o env `wokwi` pelo env `qemu` no `platformio.ini`**

Trocar o bloco `[env:wokwi]` inteiro por:

```ini
[env:qemu]
extends = env:esp32doit-devkit-v1
framework = arduino, espidf
build_flags =
    -DQEMU_EMULATOR
    -DTELEMETRY_INTERVAL_MS=10000
```

- [ ] **Step 2: Criar `sdkconfig.defaults`**

Basear-se no exemplo oficial do platform (`~/.platformio/platforms/espressif32/examples/espidf-arduino-blink/sdkconfig.defaults`) — copiar as opções obrigatórias do Arduino de lá (no mínimo `CONFIG_FREERTOS_HZ=1000` e `CONFIG_AUTOSTART_ARDUINO=y`; incluir as demais que o exemplo trouxer) e acrescentar:

```
CONFIG_ETH_USE_OPENETH=y
```

Nota: o `sdkconfig.defaults` só é lido pelo env híbrido; o env de produção (arduino puro, libs pré-compiladas) ignora o arquivo.

- [ ] **Step 3: Compilar o env qemu**

Run: `export PATH="$HOME/.platformio/penv/bin:$PATH" && pio run -e qemu`
Expected: SUCCESS (primeiro build compila o IDF inteiro: 5–15 min; timeout generoso). Erros prováveis e resolução: opção Kconfig faltante reclamada no log → adicionar ao `sdkconfig.defaults` o valor que o log pedir; conflito de partição → garantir que `board_build.partitions = min_spiffs.csv` está herdado.

- [ ] **Step 4: Provar que o openeth entrou no build**

Run: `grep -r "esp_eth_mac_new_openeth" .pio/build/qemu/ --include="*.a" -l | head -1`
Expected: pelo menos uma lib (ex.: `libesp_eth.a`) contém o símbolo. Alternativa: `nm .pio/build/qemu/esp-idf/esp_eth/libesp_eth.a 2>/dev/null | grep openeth`.

- [ ] **Step 5: Produção continua intocada**

Run: `pio run -e esp32doit-devkit-v1`
Expected: SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add platformio.ini sdkconfig.defaults CMakeLists.txt src/CMakeLists.txt 2>/dev/null; git add platformio.ini sdkconfig.defaults
git commit -m "build: env qemu híbrido arduino+espidf com openeth habilitado"
```

(Incluir os CMakeLists no add apenas se foram criados.)

---

### Task 2: Rede por Ethernet virtual + helper netReady + flag renomeado

**Files:**
- Create: `src/net/net.h`, `src/net/net.cpp`
- Modify: `include/config.h` (bloco `WOKWI_*` → `QEMU_*`/`EMU_*`)
- Modify: `src/wifi_setup/wifi_setup.cpp` (bloco do emulador)
- Modify: `src/storage/storage.cpp`, `src/identity/identity.cpp` (renomear guard)
- Modify: `src/device/device.cpp:16`, `src/telemetry/telemetry.cpp:144`, `src/http_local/http_local.cpp:1076,1082` (usar netReady/netLocalIp)

**Interfaces:**
- Produces: `bool netReady()` e `String netLocalIp()` em `src/net/net.h` — produção delega a WiFi, emulador ao estado da Ethernet. `emuNetStart()` chamado pelo `setupWifi` do emulador.

- [ ] **Step 1: `include/config.h` — renomear o bloco do emulador**

Substituir o bloco `#ifdef WOKWI_EMULATOR ... #endif` por:

```c
// ── Emulador QEMU (só existe no env `qemu`) ──────────────────────────────
#ifdef QEMU_EMULATOR
// 10.0.2.2 = host visto de dentro da rede slirp do QEMU.
#define EMU_SERVER_EXT_NAME   "raiznetd-public"
#define EMU_SERVER_EXT_URL    "http://10.0.2.2:3000/v1/telemetry"
#define EMU_SERVER_LOCAL_NAME "raiznetd-local"
#define EMU_SERVER_LOCAL_URL  "10.0.2.2:3001"
#endif
```

- [ ] **Step 2: Criar `src/net/net.h`**

```cpp
#pragma once
#include <Arduino.h>

// Abstrai "a rede está de pé?" para produção (Wi-Fi) e emulador (Ethernet
// virtual do QEMU). Só os call sites de status usam isto; o resto do código
// (HTTPClient/WiFiClient) é TCP genérico e não muda.
bool   netReady();
String netLocalIp();

#ifdef QEMU_EMULATOR
// Sobe o driver openeth e bloqueia até obter IP via DHCP (slirp).
void emuNetStart();
#endif
```

- [ ] **Step 3: Criar `src/net/net.cpp`**

```cpp
#include "net.h"
#include <WiFi.h>

#ifdef QEMU_EMULATOR
#include <esp_eth.h>
#include <esp_event.h>
#include <esp_netif.h>

static volatile bool     gGotIp = false;
static esp_netif_t*      gNetif = nullptr;

static void onGotIp(void*, esp_event_base_t, int32_t, void*) { gGotIp = true; }

void emuNetStart() {
  // O core Arduino pode já ter criado netif/event loop; erros de
  // "já inicializado" são esperados e inofensivos aqui.
  esp_netif_init();
  esp_event_loop_create_default();

  esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
  gNetif = esp_netif_new(&netif_cfg);

  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.autonego_timeout_ms = 100;
  esp_eth_mac_t* mac = esp_eth_mac_new_openeth(&mac_config);
  esp_eth_phy_t* phy = esp_eth_phy_new_dp83848(&phy_config);

  esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
  esp_eth_handle_t eth_handle = nullptr;
  ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));
  ESP_ERROR_CHECK(esp_netif_attach(gNetif, esp_eth_new_netif_glue(eth_handle)));
  esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &onGotIp, nullptr);
  ESP_ERROR_CHECK(esp_eth_start(eth_handle));

  Serial.print("[net] Esperando IP da Ethernet virtual (QEMU)");
  while (!gGotIp) { delay(100); Serial.print('.'); }
  Serial.println();
  Serial.printf("[net] Conectado, IP %s\n", netLocalIp().c_str());
}

bool netReady() { return gGotIp; }

String netLocalIp() {
  esp_netif_ip_info_t info;
  if (gNetif && esp_netif_get_ip_info(gNetif, &info) == ESP_OK) {
    return IPAddress(info.ip.addr).toString();
  }
  return "0.0.0.0";
}
#else
bool   netReady()   { return WiFi.status() == WL_CONNECTED; }
String netLocalIp() { return WiFi.localIP().toString(); }
#endif
```

- [ ] **Step 4: Substituir o bloco do emulador em `setupWifi`**

Em `src/wifi_setup/wifi_setup.cpp`, trocar o bloco `#ifdef WOKWI_EMULATOR ... #endif` no início de `setupWifi` por:

```cpp
#ifdef QEMU_EMULATOR
  // Emulador: sem portal captivo e sem rádio Wi-Fi — Ethernet virtual.
  emuNetStart();

  String emuMac = WiFi.macAddress();  // core resolve via eFuse mesmo sem Wi-Fi
  emuMac.replace(":", "");
  String emuSuffix = emuMac.substring(emuMac.length() - 4);
  emuSuffix.toLowerCase();
  mdnsName = "safrasense-aqua-" + emuSuffix;

  configTime(NTP_GMT_OFFSET_SEC, NTP_DAYLIGHT_SEC, NTP_SERVER_1, NTP_SERVER_2);
  return;
#endif
```

E adicionar `#include "net/net.h"` junto aos includes do arquivo. (Sem `MDNS.begin` — mDNS não opera sobre slirp.)

- [ ] **Step 5: Renomear o guard nos demais arquivos**

- `src/storage/storage.cpp`: `#ifdef WOKWI_EMULATOR` → `#ifdef QEMU_EMULATOR`; `WOKWI_SERVER_*` → `EMU_SERVER_*`.
- `src/identity/identity.cpp`: `#ifdef WOKWI_EMULATOR` → `#ifdef QEMU_EMULATOR` (comentário idem).

- [ ] **Step 6: Call sites de status usam o helper**

Com `#include "net/net.h"` em cada arquivo:
- `src/device/device.cpp:16`: `if (baseUrl.isEmpty() || !netReady()) return false;`
- `src/telemetry/telemetry.cpp:144`: `if (!gCfg || !gId || !netReady()) return;`
- `src/http_local/http_local.cpp:1076`: `bool wifiOk = netReady();`
- `src/http_local/http_local.cpp:1082`: `doc["ip"] = netLocalIp();`

- [ ] **Step 7: Compilar os dois envs**

Run: `pio run -e qemu && pio run -e esp32doit-devkit-v1`
Expected: SUCCESS nos dois.

- [ ] **Step 8: Commit**

```bash
git add include/config.h src/net/ src/wifi_setup/wifi_setup.cpp src/storage/storage.cpp src/identity/identity.cpp src/device/device.cpp src/telemetry/telemetry.cpp src/http_local/http_local.cpp
git commit -m "feat(emu): rede via openeth no QEMU e helper netReady"
```

---

### Task 3: Sensores sintéticos no emulador

**Files:**
- Modify: `src/sensors/sensors.cpp` (`readSensors`)

**Interfaces:**
- Produces: no env qemu, `readSensors()` retorna leituras determinísticas plausíveis (ec/temp/humidity/battery ok; laser ausente) para a telemetria carregar valores reais no e2e.

- [ ] **Step 1: Bloco sintético em `readSensors`**

No início de `SensorData readSensors()`, inserir:

```cpp
#ifdef QEMU_EMULATOR
  // QEMU não emula DHT/ADC. Valores fixos plausíveis mantêm o pipeline de
  // telemetria exercitado de ponta a ponta; o laser fica ausente como no
  // hardware sem VL53L0X.
  SensorData emu;
  emu.captured_at        = millis();
  emu.ec                 = 1412.0f;
  emu.temp_ambient       = 24.5f;
  emu.humidity           = 61.2f;
  emu.bat_volts          = 3.9f;
  emu.bat_percent        = 70;
  emu.water_level        = -1;
  emu.status.dht_ok      = true;
  emu.status.tds_ok      = true;
  emu.status.laser_ok    = true;   // ausente ≠ falho: não acender LED de erro
  emu.status.battery_ok  = true;
  return emu;
#endif
```

(Conferir os nomes exatos dos campos em `src/sensors/sensors.h` antes de aplicar; ajustar se o struct usar outros nomes/defaults.)

- [ ] **Step 2: Compilar os dois envs**

Run: `pio run -e qemu && pio run -e esp32doit-devkit-v1`
Expected: SUCCESS nos dois.

- [ ] **Step 3: Commit**

```bash
git add src/sensors/sensors.cpp
git commit -m "feat(emu): leituras sintéticas de sensores no QEMU"
```

---

### Task 4: run.sh com QEMU + imagem de flash + remoção dos artefatos Wokwi

**Files:**
- Delete: `emu/diagram.json`, `emu/wokwi.toml`
- Modify: `emu/run.sh`, `emu/README.md`
- Modify: `.gitignore` (adicionar `emu/.qemu/` e `emu/flash.bin`)

**Interfaces:**
- Consumes: build do env qemu (Task 1); firmware com rede openeth (Task 2).
- Produces: `emu/run.sh` sobe raiznetd + QEMU interativo; função reutilizável de montagem da flash (`emu/mkflash.sh`) usada também pelo e2e (Task 5).

- [ ] **Step 1: Remover artefatos Wokwi e atualizar .gitignore**

```bash
git rm emu/diagram.json emu/wokwi.toml
printf 'emu/.qemu/\nemu/flash.bin\n' >> .gitignore
```

- [ ] **Step 2: Criar `emu/mkflash.sh`** (chmod +x)

```bash
#!/usr/bin/env bash
# Monta a imagem de flash 4MB para o QEMU a partir do build do env qemu.
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD=.pio/build/qemu
ESPTOOL="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
OUT=emu/flash.bin

# O build híbrido gera bootloader.bin/partitions.bin/firmware.bin; o offset
# do ota_data (0xe000) só entra se o arquivo existir no build.
ARGS=(0x1000 "$BUILD/bootloader.bin" 0x8000 "$BUILD/partitions.bin" 0x10000 "$BUILD/firmware.bin")
for f in ota_data_initial.bin boot_app0.bin; do
  if [ -f "$BUILD/$f" ]; then ARGS=(0xe000 "$BUILD/$f" "${ARGS[@]}"); break; fi
done

python3 "$ESPTOOL" --chip esp32 merge_bin -o "$OUT" \
  --flash_mode dio --flash_freq 40m --flash_size 4MB --fill-flash-size 4MB \
  "${ARGS[@]}"
echo "[mkflash] $OUT pronto ($(stat -c%s "$OUT") bytes)"
```

- [ ] **Step 3: Criar helper de download do QEMU dentro do run.sh e adaptar o script**

Substituir o conteúdo de `emu/run.sh` por:

```bash
#!/usr/bin/env bash
# Dev interativo: compila o firmware, sobe o raiznetd local e roda o QEMU.
# Tudo local: sem token, sem nuvem. Host visto do firmware = 10.0.2.2.
set -euo pipefail
cd "$(dirname "$0")/.."

RAIZNET_DIR="${RAIZNET_DIR:-../Raiznet-rust}"
QEMU_URL="https://github.com/espressif/qemu/releases/download/esp-develop-9.0.0-20240606/qemu-xtensa-softmmu-esp_develop_9.0.0_20240606-x86_64-linux-gnu.tar.xz"
QEMU_BIN="emu/.qemu/qemu/bin/qemu-system-xtensa"

DATA_DIR_CREATED=0
if [ -z "${DATA_DIR:-}" ]; then
  DATA_DIR="$(mktemp -d /tmp/raiznetd-emu.XXXXXX)"
  DATA_DIR_CREATED=1
fi

if [ ! -x "$QEMU_BIN" ]; then
  echo "[run] Baixando QEMU da Espressif (uma vez)..."
  mkdir -p emu/.qemu
  curl -L "$QEMU_URL" | tar -xJ -C emu/.qemu
fi

export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e qemu
emu/mkflash.sh

echo "[run] raiznetd: dados em $DATA_DIR, portas 3000 (público) / 3001 (local)"
cargo build -q -p raiznetd --manifest-path "$RAIZNET_DIR/Cargo.toml"
RAIZNET_DATA_DIR="$DATA_DIR" "$RAIZNET_DIR/target/debug/raiznetd" &
RAIZNETD_PID=$!

cleanup() {
  kill "$RAIZNETD_PID" 2>/dev/null || true
  wait "$RAIZNETD_PID" 2>/dev/null || true
  [ "$DATA_DIR_CREATED" = 1 ] && rm -rf "$DATA_DIR"
}
trap cleanup EXIT INT TERM

for _ in $(seq 1 30); do
  curl -sf http://127.0.0.1:3000/health >/dev/null 2>&1 && break
  sleep 1
done
curl -sf http://127.0.0.1:3000/health >/dev/null || { echo "[run] raiznetd não subiu"; exit 1; }

# UI local do firmware: http://localhost:8180 (hostfwd → porta 80 do device).
"$QEMU_BIN" -nographic -machine esp32 \
  -drive file=emu/flash.bin,if=mtd,format=raw \
  -nic user,model=open_eth,hostfwd=tcp:127.0.0.1:8180-:80 \
  -serial mon:stdio
```

Nota para o executor: se o tar da release tiver estrutura de diretório diferente (`qemu/bin/...`), ajustar `QEMU_BIN` pelo conteúdo real e refletir a mesma constante no e2e (Task 5). Sair do QEMU interativo: `Ctrl+A, X`.

- [ ] **Step 4: Atualizar `emu/README.md`**

Substituir o conteúdo por:

```markdown
# Emulador local (QEMU Espressif)

Roda o firmware real (env `qemu`, build híbrido arduino+espidf) num ESP32
emulado **na sua máquina**, falando com o `raiznetd` (worktree
`../Raiznet-rust`). Sem token, sem nuvem.

## Como funciona

- Rede: Ethernet virtual `openeth` + slirp. O host é `10.0.2.2`
  (portas 3000/3001 do raiznetd). Wi-Fi/portal captivo não existem no
  emulador (flag `QEMU_EMULATOR`).
- Identidade fixa e telemetria a cada 10 s; sensores retornam valores
  sintéticos determinísticos.
- A flash (`emu/flash.bin`) persiste entre execuções — bom para testar
  NVS; apague o arquivo para "fábrica".

## Uso

- `emu/run.sh` — dev interativo (serial no terminal; sair: Ctrl+A, X).
  - UI local do firmware: <http://localhost:8180>
  - Inspecionar o servidor: `curl -s localhost:3000/v1/devices | jq`
- `emu/e2e.sh` — teste ponta a ponta (registro, telemetria nos dois
  listeners, re-registro após wipe do banco). Exit ≠ 0 em falha.

O QEMU da Espressif é baixado automaticamente na primeira execução para
`emu/.qemu/` (git-ignorado). Dependências: `pio`, `cargo`, `curl`, `jq`,
`python3`.
```

- [ ] **Step 5: Smoke manual**

Run: `emu/run.sh` (primeira vez baixa o QEMU). Expected no serial: `[net] Conectado, IP 10.0.2.15`, registro sem erro, ciclos de telemetria a cada ~10 s. Em outro terminal: `curl -s localhost:3000/v1/devices | jq '.devices | length'` → `1`; `curl -s localhost:8180/ | head -c 200` responde (UI do firmware). Encerrar com Ctrl+A, X e conferir que as portas 3000/3001 foram liberadas.

- [ ] **Step 6: Commit**

```bash
git add -A emu/ .gitignore
git commit -m "feat(emu): QEMU local no lugar do Wokwi (mkflash, run.sh, README)"
```

---

### Task 5: e2e.sh no QEMU + execução real

**Files:**
- Modify: `emu/e2e.sh`

**Interfaces:**
- Consumes: `emu/mkflash.sh`, `$QEMU_BIN` (Task 4), firmware das Tasks 1–3.
- Produces: `emu/e2e.sh` verde, cobrindo os critérios 1–4 da spec (agora executável de verdade, sem token).

- [ ] **Step 1: Adaptar `emu/e2e.sh`**

Manter a estrutura existente (fases, `wait_readings`, `start/stop_raiznetd` com binário direto, trap) e trocar só a parte do simulador:

- Remover a checagem/uso de `wokwi-cli`.
- Após `pio run -e qemu >/dev/null`: `emu/mkflash.sh >/dev/null` e **remover `emu/flash.bin` antes** (`rm -f emu/flash.bin`) para o e2e sempre partir de NVS zerada (identidade é fixa por flag, então o device_id não muda).
- Definir no topo: `QEMU_BIN="emu/.qemu/qemu/bin/qemu-system-xtensa"` e, se não existir, baixar como no run.sh (mesma URL pinada).
- Substituir o start do simulador por:

```bash
"$QEMU_BIN" -nographic -machine esp32 \
  -drive file=emu/flash.bin,if=mtd,format=raw \
  -nic user,model=open_eth \
  -serial file:"$WOKWI_LOG" >/dev/null 2>&1 &
WOKWI_PID=$!
```

(Renomear as variáveis `WOKWI_LOG`/`WOKWI_PID` para `EMU_LOG`/`EMU_PID` em todo o script, incluindo o `cleanup`.)

- [ ] **Step 2: Rodar o e2e completo**

Run: `emu/e2e.sh`
Expected:

```
e2e fase 1: registro + telemetria nos dois listeners
e2e fase 2: banco recriado → re-registro automático (fix do 207)
e2e OK
```

Exit 0. Se a fase 1 falhar, inspecionar o log do emulador (caminho impresso no cleanup) procurando `[net]`/`[telemetry]`; causas prováveis: estrutura do tar do QEMU (ajustar QEMU_BIN), DHCP não veio (conferir `-nic user,model=open_eth`).

- [ ] **Step 3: Commit**

```bash
git add emu/e2e.sh
git commit -m "test(emu): e2e no QEMU local, executado de ponta a ponta"
```

---

### Task 6: Verificação final contra a spec revisada

**Files:** nenhum novo; verificação e eventuais ajustes.

- [ ] **Step 1: Produção intocada**

Run: `pio run -e esp32doit-devkit-v1`
Expected: SUCCESS. `grep -rn "QEMU_EMULATOR\|WOKWI" src/ include/ platformio.ini` → nenhuma referência a WOKWI sobrando; todo QEMU_EMULATOR dentro de guards.

- [ ] **Step 2: e2e do zero**

Run: `rm -f emu/flash.bin && emu/e2e.sh`
Expected: `e2e OK`, exit 0.

- [ ] **Step 3: Smoke interativo curto**

Run: `emu/run.sh` por ~30 s; conferir UI em localhost:8180 e `curl` de devices; Ctrl+A X; portas liberadas.

- [ ] **Step 4: Commit final (se houve ajustes)**

```bash
git status --short
# se houver mudanças: git add -A && git commit -m "chore(emu): ajustes finais do emulador QEMU"
```

---

## Self-Review (feita na escrita)

- **Cobertura da spec revisada:** env qemu híbrido+openeth (T1) ✓; rede openeth + netReady + defaults 10.0.2.2 + rename do flag (T2) ✓; sensores sintéticos (T3) ✓; scripts/QEMU/remoção Wokwi + flash persistente documentada (T4) ✓; e2e executado de verdade (T5) ✓; produção intocada (T6) ✓. Fix do 207 e identidade fixa: já commitados, permanecem.
- **Placeholders:** nenhum; pontos dependentes de ambiente (estrutura do tar do QEMU, nomes de campos do SensorData, opções extras de Kconfig) têm instrução explícita de resolução pela evidência local.
- **Consistência:** `QEMU_EMULATOR`/`EMU_SERVER_*` usados consistentemente em T1/T2; `emu/mkflash.sh` definido em T4 e consumido em T5; caminho `QEMU_BIN` único definido em T4 e reutilizado em T5.
