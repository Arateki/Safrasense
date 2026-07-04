# Emulador headless Wokwi + fix do 207 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rodar o firmware Safrasense completo num ESP32 emulado (Wokwi CLI, headless), falando com o `raiznetd` (Rust) local, com teste e2e scriptado — e corrigir o tratamento do 207 na telemetria.

**Architecture:** Um env `wokwi` no PlatformIO adiciona `-DWOKWI_EMULATOR`; sob esse flag o firmware pula o portal captivo (conecta na rede virtual `Wokwi-GUEST`), usa servidores default apontando para `host.wokwi.internal:3000/3001` (raiznetd no host) e identidade determinística. A pasta `emu/` contém o circuito virtual, a config do Wokwi e os scripts `run.sh` (dev interativo) e `e2e.sh` (teste ponta a ponta, incluindo o cenário de banco recriado que valida o fix do 207).

**Tech Stack:** PlatformIO (Arduino/ESP32), Wokwi CLI, bash + curl + jq, raiznetd (cargo, worktree `../Raiznet-rust`).

## Global Constraints

- O env de produção `esp32doit-devkit-v1` compila sem NENHUMA mudança de comportamento; todo código de emulador fica atrás de `#ifdef WOKWI_EMULATOR`.
- Servidores do emulador: externo `http://host.wokwi.internal:3000/v1/telemetry`, local `host.wokwi.internal:3001`. NUNCA `raiznet.com`.
- raiznetd: worktree `../Raiznet-rust` (branch `rust-migration`), portas default 3000 (público) e 3001 (local), config por env vars `RAIZNET_DATA_DIR` etc.
- A string `"Device not found"` no `errors[].error` do 207 é CONTRATO dos dois servidores (ver `Raiznet-rust/apps/raiznetd/src/domain/errors.rs`) — é o gatilho do re-registro.
- Mensagens de commit sem menção a Claude e sem trailer `Co-Authored-By` (preferência do Yan).
- Pré-requisitos da máquina: `pio` (PlatformIO CLI), `wokwi-cli` + `WOKWI_CLI_TOKEN` (token gratuito em wokwi.com → CLI), `cargo`, `curl`, `jq`.

---

### Task 1: Env `wokwi` no PlatformIO + intervalo de telemetria configurável

**Files:**
- Modify: `platformio.ini`
- Modify: `include/config.h:28`

**Interfaces:**
- Produces: env de build `wokwi` (`pio run -e wokwi`) com os defines `WOKWI_EMULATOR` e `TELEMETRY_INTERVAL_MS=10000`; binário em `.pio/build/wokwi/firmware.bin`. Tasks 2–6 dependem desse env.

- [ ] **Step 1: Adicionar o env `wokwi` ao `platformio.ini`**

Acrescentar ao final do arquivo:

```ini
[env:wokwi]
extends = env:esp32doit-devkit-v1
build_flags =
    -DWOKWI_EMULATOR
    -DTELEMETRY_INTERVAL_MS=10000
```

- [ ] **Step 2: Tornar `TELEMETRY_INTERVAL_MS` sobrescrevível**

Em `include/config.h`, trocar a linha `#define TELEMETRY_INTERVAL_MS   60000   // 60s` por:

```c
#ifndef TELEMETRY_INTERVAL_MS
#define TELEMETRY_INTERVAL_MS   60000   // 60s (o env wokwi sobrescreve via build_flags)
#endif
```

- [ ] **Step 3: Verificar que os dois envs compilam**

Run: `pio run -e wokwi && pio run -e esp32doit-devkit-v1`
Expected: `SUCCESS` nos dois; existe `.pio/build/wokwi/firmware.bin`.

- [ ] **Step 4: Commit**

```bash
git add platformio.ini include/config.h
git commit -m "build: env wokwi com WOKWI_EMULATOR e telemetria acelerada"
```

---

### Task 2: Wi-Fi direto (sem portal) e servidores default do emulador

**Files:**
- Modify: `include/config.h` (novo bloco de defines)
- Modify: `src/wifi_setup/wifi_setup.cpp:1394` (`setupWifi`)
- Modify: `src/storage/storage.cpp:18-25` (`loadConfig`, bloco de primeiro boot)

**Interfaces:**
- Consumes: define `WOKWI_EMULATOR` (Task 1).
- Produces: no env wokwi, o boot conecta na `Wokwi-GUEST` sem portal e a config default aponta para o raiznetd do host. Nada muda no env de produção.

- [ ] **Step 1: Defines do emulador em `include/config.h`**

Acrescentar ao final do arquivo:

```c
// ── Emulador Wokwi (só existe no env `wokwi`) ─────────────────────────────
#ifdef WOKWI_EMULATOR
#define WOKWI_WIFI_SSID         "Wokwi-GUEST"
#define WOKWI_SERVER_EXT_NAME   "raiznetd-public"
#define WOKWI_SERVER_EXT_URL    "http://host.wokwi.internal:3000/v1/telemetry"
#define WOKWI_SERVER_LOCAL_NAME "raiznetd-local"
#define WOKWI_SERVER_LOCAL_URL  "host.wokwi.internal:3001"
#endif
```

- [ ] **Step 2: Bypass do portal em `setupWifi`**

Em `src/wifi_setup/wifi_setup.cpp`, logo após a abertura de `void setupWifi(DeviceConfig& cfg) {`, inserir:

```cpp
#ifdef WOKWI_EMULATOR
  // Emulador: sem portal captivo. Conecta direto na rede virtual do Wokwi.
  WiFi.mode(WIFI_STA);
  WiFi.begin(WOKWI_WIFI_SSID, "", 6);  // canal 6 acelera o associate no Wokwi
  Serial.print("[wifi] Conectando na Wokwi-GUEST");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print('.');
  }
  Serial.printf("\n[wifi] Conectado, IP %s\n", WiFi.localIP().toString().c_str());

  String emuMac = WiFi.macAddress();
  emuMac.replace(":", "");
  String emuSuffix = emuMac.substring(emuMac.length() - 4);
  emuSuffix.toLowerCase();
  mdnsName = "safrasense-aqua-" + emuSuffix;

  configTime(NTP_GMT_OFFSET_SEC, NTP_DAYLIGHT_SEC, NTP_SERVER_1, NTP_SERVER_2);
  MDNS.begin(mdnsName.c_str());
  return;
#endif
```

O corpo original segue intacto abaixo do bloco. `reconnectWifi` não precisa de mudança: `WiFi.begin()` sem argumentos reusa as credenciais da sessão.

- [ ] **Step 3: Defaults de servidores em `loadConfig`**

Em `src/storage/storage.cpp`, no bloco `if (json.isEmpty())`, trocar a linha `cfg.servers_external.push_back({ DEFAULT_SERVER_EXT_NAME, DEFAULT_SERVER_EXT_URL });` por:

```cpp
#ifdef WOKWI_EMULATOR
    // Emulador: raiznetd do host — nunca o servidor de produção.
    cfg.servers_external.push_back({ WOKWI_SERVER_EXT_NAME, WOKWI_SERVER_EXT_URL });
    cfg.servers_local.push_back({ WOKWI_SERVER_LOCAL_NAME, WOKWI_SERVER_LOCAL_URL });
#else
    cfg.servers_external.push_back({ DEFAULT_SERVER_EXT_NAME, DEFAULT_SERVER_EXT_URL });
#endif
```

- [ ] **Step 4: Verificar que os dois envs compilam**

Run: `pio run -e wokwi && pio run -e esp32doit-devkit-v1`
Expected: `SUCCESS` nos dois.

- [ ] **Step 5: Commit**

```bash
git add include/config.h src/wifi_setup/wifi_setup.cpp src/storage/storage.cpp
git commit -m "feat(emu): Wi-Fi direto na Wokwi-GUEST e servidores default do emulador"
```

---

### Task 3: Identidade determinística no emulador

**Files:**
- Modify: `src/identity/identity.cpp:30-52` (`loadOrCreateIdentity`)

**Interfaces:**
- Consumes: `importOwnerIdentity(DeviceIdentity&, String)` e `bytesToHex` já existentes no mesmo arquivo.
- Produces: no env wokwi, `loadOrCreateIdentity()` retorna sempre a MESMA identidade (device e owner), então o device_id é estável entre execuções e o registro nunca falha por `ownerPubkey` vazio. O e2e (Task 6) depende de existir exatamente 1 device no banco.

- [ ] **Step 1: Versão do emulador de `loadOrCreateIdentity`**

Em `src/identity/identity.cpp`, envolver a função existente assim (a versão original fica no `#else`):

```cpp
#ifdef WOKWI_EMULATOR
// Emulador: o Wokwi não persiste NVS entre execuções. Identidade fixa mantém
// o device_id estável e o owner determinístico evita registro 400
// (ownerPubkey vazio antes do onboarding).
DeviceIdentity loadOrCreateIdentity() {
  DeviceIdentity id;
  id.mac = WiFi.macAddress();
  static const uint8_t kFixedSeed[32] = {
    'S', 'a', 'f', 'r', 'a', 's', 'e', 'n',
    's', 'e', '-', 'W', 'o', 'k', 'w', 'i',
    '-', 'd', 'e', 'v', '-', '0', '0', '0',
    '1', 0, 0, 0, 0, 0, 0, 0,
  };
  memcpy(id.private_key, kFixedSeed, 32);
  Ed25519::derivePublicKey(id.public_key, id.private_key);
  id.public_key_hex = bytesToHex(id.public_key, 32);
  importOwnerIdentity(id,
    "abandon abandon abandon abandon abandon abandon "
    "abandon abandon abandon abandon abandon about");
  id.lang = LANG_PT;
  return id;
}
#else
DeviceIdentity loadOrCreateIdentity() {
  // ... corpo original inalterado ...
}
#endif
```

(`importOwnerIdentity` deriva as chaves do owner de SHA256(mnemonic) — determinístico. A mnemonic fixa é a mesma usada nos testes do raiznetd, só por conveniência de leitura; a derivação é diferente e isso não importa aqui.)

- [ ] **Step 2: Verificar que os dois envs compilam**

Run: `pio run -e wokwi && pio run -e esp32doit-devkit-v1`
Expected: `SUCCESS` nos dois.

- [ ] **Step 3: Commit**

```bash
git add src/identity/identity.cpp
git commit -m "feat(emu): identidade determinística no emulador Wokwi"
```

---

### Task 4: Fix do 207 na telemetria

**Files:**
- Modify: `src/telemetry/telemetry.cpp:102-121` (`postTelemetry`)

**Interfaces:**
- Consumes: `invalidateDeviceRegistry()` de `src/device/device.h` (já incluído no arquivo).
- Produces: em resposta 207 cujo body contenha `"Device not found"`, o firmware invalida o registro local; no ciclo seguinte `sendPending()` re-registra (via `syncDeviceRegistry`) e reenvia o mesmo bloco. A fase 2 do e2e (Task 6) valida esse comportamento.

- [ ] **Step 1: Reescrever `postTelemetry`**

Substituir a função inteira por:

```cpp
static bool postTelemetry(const String& url, const String& body) {
  if (url.isEmpty()) return false;
  HTTPClient http;
  http.setTimeout(8000);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);

  if (code == 200) {
    http.end();
    return true;
  }

  // O servidor responde 207 com o erro por bloco no body — nunca 404 no
  // /v1/telemetry. Como enviamos 1 bloco por request, 207 = este bloco falhou.
  // "Device not found" é contrato dos dois servidores (TS e Rust): o servidor
  // não conhece este device (ex.: banco recriado). Invalida o registro local
  // para o próximo ciclo re-registrar e reenviar este mesmo bloco.
  if (code == 207) {
    String resp = http.getString();
    http.end();
    if (resp.indexOf("Device not found") >= 0) {
      Serial.printf("[telemetry] Servidor %s não conhece o device. Invalidando registro.\n", url.c_str());
      invalidateDeviceRegistry();
    } else {
      Serial.printf("[telemetry] Bloco rejeitado por %s: %s\n", url.c_str(), resp.c_str());
    }
    return false;
  }

  http.end();

  // Defesa: servidores antigos podem sinalizar device desconhecido com 404.
  if (code == 404) {
    Serial.printf("[telemetry] Servidor %s retornou 404. Invalidando registro.\n", url.c_str());
    invalidateDeviceRegistry();
  }

  return false;
}
```

- [ ] **Step 2: Verificar que os dois envs compilam**

Run: `pio run -e wokwi && pio run -e esp32doit-devkit-v1`
Expected: `SUCCESS` nos dois.

- [ ] **Step 3: Commit**

```bash
git add src/telemetry/telemetry.cpp
git commit -m "fix(telemetry): re-registro em 207 device-not-found (servidor nunca envia 404)"
```

---

### Task 5: Circuito virtual, config do Wokwi e `run.sh`

**Files:**
- Create: `emu/diagram.json`
- Create: `emu/wokwi.toml`
- Create: `emu/run.sh`
- Create: `emu/README.md`

**Interfaces:**
- Consumes: binário `.pio/build/wokwi/firmware.{bin,elf}` (Task 1); firmware adaptado (Tasks 2–4).
- Produces: `emu/run.sh` sobe raiznetd + emulador interativo. `emu/` com `wokwi.toml` + `diagram.json` é o que o `wokwi-cli` consome (Task 6 reusa).

- [ ] **Step 1: Criar `emu/diagram.json`**

```json
{
  "version": 1,
  "author": "Safrasense",
  "editor": "wokwi",
  "parts": [
    { "type": "wokwi-esp32-devkit-v1", "id": "esp", "top": 0, "left": 0 },
    { "type": "wokwi-dht22", "id": "dht", "top": -90, "left": 220 },
    { "type": "wokwi-potentiometer", "id": "tds", "top": 60, "left": 220 },
    { "type": "wokwi-potentiometer", "id": "bat", "top": 170, "left": 220 },
    { "type": "wokwi-led", "id": "ledR", "top": -80, "left": -120, "attrs": { "color": "red" } },
    { "type": "wokwi-led", "id": "ledY", "top": 0, "left": -120, "attrs": { "color": "yellow" } },
    { "type": "wokwi-led", "id": "ledG", "top": 80, "left": -120, "attrs": { "color": "green" } }
  ],
  "connections": [
    [ "esp:TX0", "$serialMonitor:RX", "", [] ],
    [ "esp:RX0", "$serialMonitor:TX", "", [] ],
    [ "dht:VCC", "esp:3V3", "red", [] ],
    [ "dht:GND", "esp:GND.1", "black", [] ],
    [ "dht:SDA", "esp:D4", "green", [] ],
    [ "tds:VCC", "esp:3V3", "red", [] ],
    [ "tds:GND", "esp:GND.1", "black", [] ],
    [ "tds:SIG", "esp:D34", "green", [] ],
    [ "bat:VCC", "esp:3V3", "red", [] ],
    [ "bat:GND", "esp:GND.1", "black", [] ],
    [ "bat:SIG", "esp:D35", "green", [] ],
    [ "ledR:A", "esp:D14", "red", [] ],
    [ "ledR:C", "esp:GND.1", "black", [] ],
    [ "ledY:A", "esp:D12", "yellow", [] ],
    [ "ledY:C", "esp:GND.1", "black", [] ],
    [ "ledG:A", "esp:D13", "green", [] ]
  ]
}
```

Notas para o executor: (a) o VL53L0X não existe no Wokwi — o firmware loga "VL53L0X não encontrado" e segue; (b) os pinos de power-gating (GPIO25/26) ficam desconectados — DHT e potenciômetros ligados direto no 3V3; (c) se o `wokwi-cli` reclamar de algum nome de pino (ex.: `GND.1` vs `GND.2`), ajustar pelo erro reportado — a peça diz quais pinos tem.

- [ ] **Step 2: Criar `emu/wokwi.toml`**

```toml
[wokwi]
version = 1
firmware = "../.pio/build/wokwi/firmware.bin"
elf = "../.pio/build/wokwi/firmware.elf"

# UI local do firmware: http://localhost:8180 no navegador do host.
[[net.forward]]
from = "localhost:8180"
to = "target:80"
```

- [ ] **Step 3: Criar `emu/run.sh`** (e `chmod +x emu/run.sh`)

```bash
#!/usr/bin/env bash
# Dev interativo: compila o firmware, sobe o raiznetd local e roda o emulador.
# Pré-requisitos: pio, cargo, curl, wokwi-cli autenticado (WOKWI_CLI_TOKEN).
set -euo pipefail
cd "$(dirname "$0")/.."

RAIZNET_DIR="${RAIZNET_DIR:-../Raiznet-rust}"
DATA_DIR="${DATA_DIR:-$(mktemp -d /tmp/raiznetd-emu.XXXXXX)}"

pio run -e wokwi

echo "[run] raiznetd: dados em $DATA_DIR, portas 3000 (público) / 3001 (local)"
(cd "$RAIZNET_DIR" && RAIZNET_DATA_DIR="$DATA_DIR" cargo run -q -p raiznetd) &
RAIZNETD_PID=$!
trap 'kill $RAIZNETD_PID 2>/dev/null || true' EXIT

for _ in $(seq 1 30); do
  curl -sf http://127.0.0.1:3000/health >/dev/null 2>&1 && break
  sleep 1
done
curl -sf http://127.0.0.1:3000/health >/dev/null || { echo "[run] raiznetd não subiu"; exit 1; }

exec wokwi-cli --interactive --timeout 0 emu
```

Nota: se `--interactive`/`--timeout` divergirem na versão instalada, conferir `wokwi-cli --help`; o essencial é serial no terminal e execução sem limite de tempo.

- [ ] **Step 4: Criar `emu/README.md`**

```markdown
# Emulador headless (Wokwi CLI)

Roda o firmware real (env `wokwi` do PlatformIO) num ESP32 emulado, falando
com o `raiznetd` (worktree `../Raiznet-rust`) na sua máquina.

## Setup (uma vez)

1. Instale o Wokwi CLI: <https://docs.wokwi.com/wokwi-ci/getting-started>
2. Crie um token gratuito em <https://wokwi.com/dashboard/ci> e exporte:
   `export WOKWI_CLI_TOKEN=...` (coloque no seu shell rc).
3. Dependências: `pio`, `cargo`, `curl`, `jq`.

## Uso

- `emu/run.sh` — dev interativo: raiznetd + emulador com serial no terminal.
  - UI local do firmware: <http://localhost:8180>
  - Inspecionar o servidor: `curl -s localhost:3000/v1/devices | jq`
- `emu/e2e.sh` — teste ponta a ponta (registro, telemetria nos dois
  listeners, re-registro após wipe do banco). Sai com código ≠ 0 em falha.

## O que difere da placa real (flag `WOKWI_EMULATOR`)

- Sem portal captivo: conecta direto na rede virtual `Wokwi-GUEST`.
- Servidores default: `host.wokwi.internal:3000/3001` (nunca produção).
- Identidade fixa (device_id estável entre execuções; NVS não persiste).
- Telemetria a cada 10 s (produção: 60 s).
- Sem VL53L0X (peça não existe no Wokwi); waterLevel não é enviado.
```

- [ ] **Step 5: Smoke manual**

Run: `emu/run.sh`
Expected no serial: `[wifi] Conectado`, registro sem erro, e a cada ~10 s um ciclo de telemetria com `last_send_ok` (LED verde/blink). Em outro terminal: `curl -s localhost:3000/v1/devices | jq '.devices | length'` → `1`, e `curl -s "localhost:3000/v1/devices/$(curl -s localhost:3000/v1/devices | jq -r '.devices[0].id')/telemetry" | jq '.readings | length'` cresce. `Ctrl+C` encerra tudo.

- [ ] **Step 6: Commit**

```bash
git add emu/
git commit -m "feat(emu): circuito Wokwi, wokwi.toml e run.sh para dev sem placa"
```

---

### Task 6: Teste e2e (`emu/e2e.sh`)

**Files:**
- Create: `emu/e2e.sh`

**Interfaces:**
- Consumes: `emu/` (Task 5), firmware com fix do 207 (Task 4), identidade fixa (Task 3 — garante exatamente 1 device no banco).
- Produces: `emu/e2e.sh` com exit code 0 = tudo ok. Cobre os critérios de aceite 1–4 da spec.

- [ ] **Step 1: Criar `emu/e2e.sh`** (e `chmod +x emu/e2e.sh`)

```bash
#!/usr/bin/env bash
# Teste e2e: firmware emulado (Wokwi) ↔ raiznetd.
#   Fase 1: banco limpo → device registra e telemetria chega nos dois listeners.
#   Fase 2: banco recriado → firmware re-registra sozinho (fix do 207).
set -euo pipefail
cd "$(dirname "$0")/.."

RAIZNET_DIR="${RAIZNET_DIR:-../Raiznet-rust}"
DATA_DIR="$(mktemp -d /tmp/raiznetd-e2e.XXXXXX)"
WOKWI_LOG="$(mktemp /tmp/wokwi-e2e.XXXXXX.log)"
RAIZNETD_PID=""
WOKWI_PID=""

pio run -e wokwi >/dev/null

start_raiznetd() {
  (cd "$RAIZNET_DIR" && RAIZNET_DATA_DIR="$DATA_DIR" cargo run -q -p raiznetd) &
  RAIZNETD_PID=$!
  for _ in $(seq 1 30); do
    curl -sf http://127.0.0.1:3000/health >/dev/null 2>&1 && return 0
    sleep 1
  done
  echo "e2e: raiznetd não subiu"
  exit 1
}

stop_raiznetd() {
  [ -n "$RAIZNETD_PID" ] && kill "$RAIZNETD_PID" 2>/dev/null || true
  wait "$RAIZNETD_PID" 2>/dev/null || true
  RAIZNETD_PID=""
}

cleanup() {
  stop_raiznetd
  [ -n "$WOKWI_PID" ] && kill "$WOKWI_PID" 2>/dev/null || true
  rm -rf "$DATA_DIR"
  echo "e2e: log do emulador em $WOKWI_LOG"
}
trap cleanup EXIT

start_raiznetd
wokwi-cli --timeout 0 emu >"$WOKWI_LOG" 2>&1 &
WOKWI_PID=$!

# Espera até $3 segundos por >= $2 leituras no listener da porta $1.
wait_readings() {
  local port=$1 min=$2 deadline=$(( $(date +%s) + $3 ))
  while [ "$(date +%s)" -lt "$deadline" ]; do
    local id
    id=$(curl -sf "http://127.0.0.1:$port/v1/devices" | jq -r '.devices[0].id // empty' || true)
    if [ -n "$id" ]; then
      local n
      n=$(curl -sf "http://127.0.0.1:$port/v1/devices/$id/telemetry" | jq '.readings | length' || echo 0)
      [ "$n" -ge "$min" ] && return 0
    fi
    sleep 2
  done
  return 1
}

echo "e2e fase 1: registro + telemetria nos dois listeners"
wait_readings 3000 1 180 || { echo "FALHOU: sem leituras no listener público"; exit 1; }
wait_readings 3001 1 60  || { echo "FALHOU: sem leituras no listener local"; exit 1; }

echo "e2e fase 2: banco recriado → re-registro automático (fix do 207)"
stop_raiznetd
rm -rf "$DATA_DIR"
mkdir -p "$DATA_DIR"
start_raiznetd
wait_readings 3000 1 120 || { echo "FALHOU: device não se re-registrou após wipe do banco"; exit 1; }

echo "e2e OK"
```

- [ ] **Step 2: Rodar o e2e**

Run: `emu/e2e.sh`
Expected:

```
e2e fase 1: registro + telemetria nos dois listeners
e2e fase 2: banco recriado → re-registro automático (fix do 207)
e2e OK
```

Exit code 0 (`echo $?`). Se a fase 2 falhar, o fix do 207 (Task 4) está quebrado — inspecionar o log do emulador (caminho impresso no final) procurando `[telemetry]`.

- [ ] **Step 3: Commit**

```bash
git add emu/e2e.sh
git commit -m "test(emu): e2e registro + telemetria + re-registro pós-wipe"
```

---

### Task 7: Verificação final contra a spec

**Files:**
- Nenhum novo; verificação e eventual ajuste.

**Interfaces:**
- Consumes: tudo acima.
- Produces: critérios de aceite da spec confirmados.

- [ ] **Step 1: Produção intocada (critério 5)**

Run: `pio run -e esp32doit-devkit-v1`
Expected: `SUCCESS`. E `grep -rn "WOKWI" src/ include/ | grep -v "#ifdef\|#ifndef\|#define WOKWI\|#else\|#endif\|WOKWI_"` não mostra código do emulador fora de guards.

- [ ] **Step 2: e2e completo do zero (critérios 1–4)**

Run: `emu/e2e.sh`
Expected: `e2e OK`, exit 0.

- [ ] **Step 3: Commit final (se houve ajustes)**

```bash
git status --short
# se houver mudanças: git add -A && git commit -m "chore(emu): ajustes finais do emulador"
```

---

## Self-Review (feita na escrita)

- **Cobertura da spec:** env wokwi (Task 1) ✓; bypass do portal + defaults (Task 2) ✓; identidade fixa + owner automático (Task 3) ✓; fix do 207 (Task 4) ✓; `emu/` com diagram/toml/run.sh + forward da UI (Task 5) ✓; e2e incluindo cenário de wipe (Task 6, critérios 1–4) ✓; produção intocada (Task 7, critério 5) ✓.
- **Placeholders:** nenhum; os dois pontos dependentes de versão de ferramenta (nomes de pinos do Wokwi, flags do wokwi-cli) têm instrução explícita de resolução pelo erro/`--help`.
- **Consistência de tipos/nomes:** `WOKWI_SERVER_*` definidos na Task 2 e usados na Task 2; `wait_readings`/`start_raiznetd` internos ao e2e; caminhos `.pio/build/wokwi/` consistentes entre Tasks 1, 5 e 6.
