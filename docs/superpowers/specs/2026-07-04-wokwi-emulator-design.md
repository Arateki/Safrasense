# Emulação headless do firmware (Wokwi CLI) + correção do 207

Data: 2026-07-04
Status: aprovado em conversa; aguardando revisão final da spec

## Contexto

O Safrasense é um firmware ESP32 (Arduino/PlatformIO) que registra o device e envia
telemetria assinada (Ed25519) para o Raiznet. O servidor está migrando de TypeScript
para Rust (`raiznetd`, no worktree `../Raiznet-rust`, branch `rust-migration`), com
paridade de protocolo verificada. Hoje todo teste do firmware é manual, com a placa
física. Não existe teste automatizado no firmware (o `test/` está vazio); o lado
servidor tem testes de unidade/paridade, mas nada de ponta a ponta com o firmware.

Durante a análise de compatibilidade firmware ↔ raiznetd foi encontrado um
descompasso real: o firmware espera `404` no `POST /v1/telemetry` para invalidar o
registro (`telemetry.cpp:115`), mas o servidor (TS e Rust) responde `207
Multi-Status` com o erro no body — nunca `404`. Se o banco do servidor for recriado,
o firmware entra em loop de falha e nunca se re-registra.

## Objetivo

1. Desenvolver e testar o firmware sem placa física, com o ciclo completo
   (boot → Wi-Fi → registro → telemetria → raiznetd → SQLite) rodando localmente.
2. Corrigir o tratamento do `207` para o re-registro automático funcionar.
3. Base para testes e2e scriptados usando o emulador.

## Decisões

- **Emulador: Wokwi CLI** (headless). Único que emula o Wi-Fi do ESP32 rodando o
  binário real do PlatformIO; o firmware alcança o host via `host.wokwi.internal`.
  QEMU/Espressif foi descartado (sem Wi-Fi para Arduino); build nativo com mocks foi
  descartado (não exercita o binário real).
- **Escopo local apenas** (sem CI por enquanto). Requer token gratuito
  (`WOKWI_CLI_TOKEN`).
- **Fix do 207 incluído** neste trabalho.

## Arquitetura

### 1. Env de build `wokwi` (`platformio.ini`)

Novo `[env:wokwi]` herdando de `esp32doit-devkit-v1`, adicionando
`-DWOKWI_EMULATOR` e um intervalo de telemetria curto (ex.: 10 s) para iteração e
e2e rápidos. O env de produção fica intocado.

### 2. Adaptações sob `#ifdef WOKWI_EMULATOR`

- `wifi_setup`: pula o portal captivo do WiFiManager; conecta direto na rede
  virtual `Wokwi-GUEST` (aberta).
- Config default do emulador:
  - servidor externo: `http://host.wokwi.internal:3000/v1/telemetry` (listener
    público do raiznetd) — nunca `raiznet.com`, para o device emulado não postar
    em produção;
  - servidor local: `host.wokwi.internal:3001` (listener local do raiznetd).
  Assim os dois destinos (public_db e private_db) são exercitados.
- Identidade: chave do device derivada de seed fixa e owner gerado
  automaticamente no boot quando ausente. Motivo: o Wokwi não persiste NVS entre
  execuções — sem seed fixa cada run criaria um device novo, e sem owner o
  registro falha com `400` (ownerPubkey vazio).

### 3. Diretório `emu/`

- `diagram.json`: ESP32 DevKit v1; DHT22 no GPIO4; potenciômetros nos ADCs
  GPIO34 (TDS) e GPIO35 (bateria); LEDs em 14/12/13; botão no GPIO0. O VL53L0X
  não existe no Wokwi; o firmware já tolera a ausência (waterLevel não é
  enviado).
- `wokwi.toml`: aponta para `firmware.bin`/`firmware.elf` do env `wokwi`;
  `[[net.forward]]` do host 8180 → porta 80 do device (UI local do firmware no
  navegador).
- `run.sh`: `pio run -e wokwi` → sobe `raiznetd` (`cargo run` no worktree
  `../Raiznet-rust`, `RAIZNET_DATA_DIR` temporário) → roda `wokwi-cli` com
  serial no terminal.
- `e2e.sh` (primeiro teste e2e): sobe `raiznetd` com banco limpo, roda o
  emulador com cenário/timeout do `wokwi-cli`, e valida via `curl`:
  device presente em `GET /v1/devices` e leituras em
  `GET /v1/devices/{id}/telemetry` (nos dois listeners). Sai com código ≠ 0 em
  falha.

### 4. Correção do 207 (`telemetry.cpp`)

Em `postTelemetry`, quando a resposta for `207`, parsear o body
(`{accepted, errors[]}` — o firmware envia 1 bloco por request):

- erro de device desconhecido → `invalidateDeviceRegistry()` (mesmo efeito que o
  `404` pretendia); no próximo ciclo o firmware re-registra e reenvia o bloco,
  sem perda de dados;
- outros erros (assinatura, raw mismatch) → log no serial; o bloco permanece
  pendente (comportamento visível em dev, que é o desejado).

O tratamento do `404` permanece, por defesa.

## Fora de escopo

- CI (GitHub Actions) — possível depois, o Wokwi CLI suporta.
- Peça customizada para o VL53L0X no Wokwi.
- Mudanças no `raiznetd` (nenhuma é necessária).

## Critérios de aceite

1. `emu/run.sh` sobe raiznetd + emulador; o serial mostra Wi-Fi conectado,
   registro `201` e telemetria `200`.
2. `curl` no raiznetd mostra o device e leituras crescendo nos dois bancos.
3. Com o emulador rodando, apagar o `RAIZNET_DATA_DIR` e reiniciar o raiznetd:
   o firmware re-registra sozinho e a telemetria volta a fluir (fix do 207).
4. `emu/e2e.sh` passa do zero e falha se a telemetria não chegar.
5. `pio run` (env de produção) continua compilando sem nenhuma referência a
   código do emulador.
