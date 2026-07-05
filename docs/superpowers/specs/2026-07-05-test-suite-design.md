# Suíte de testes anti-regressão do firmware (todas as superfícies emuláveis)

Data: 2026-07-05
Status: aprovado em conversa (Yan)

## Objetivo

Permitir que agentes desenvolvam e validem o firmware **sem hardware físico e
sem intervenção do Yan**: um comando único (`emu/test-all.sh`) exercita todas
as superfícies emuláveis e falha com exit ≠ 0 em qualquer regressão.

## Contexto

O emulador QEMU local já existe (env `qemu`, spec 2026-07-04 revisada) e o
e2e de protocolo (registro, telemetria assinada nos dois listeners do
raiznetd, re-registro pós-wipe/207) roda verde na máquina do Yan (libslirp
nativa via pacman — a máquina é Arch). Não existe nenhum teste unitário no
projeto. A UI web local do firmware tem 16 rotas sem cobertura.

## Arquitetura — 3 camadas + runner

1. **Unit/componente no QEMU** (`pio test -e qemu`): testes Unity compilados
   como firmware de teste e executados no QEMU via `test_testing_command`
   (wrapper que monta a flash e roda o QEMU com serial no stdout). Testa o
   código real com NVS/crypto/String reais — zero mocks. Alvos:
   - `identity`: validação de mnemonic (completa/parcial/inválida/sugestões,
     PT e EN no mínimo), import determinístico (mnemonic fixa → owner pubkey
     golden), assinatura Ed25519 golden (regression pinning), erase/save/load
     em NVS real.
   - `telemetry` (formato): `buildRaw`/`buildJson` extraídos para
     `src/telemetry/format.{h,cpp}` (refactor sem mudança de comportamento)
     e testados contra golden strings do contrato (§7 do plano Rust: ordem
     dos campos, decimais, seq/timestamp como string, keyVersion 0).
   - `buffer`: máscaras de confirmação por servidor, overflow (50), pendências.
   - `storage`: roundtrip save/load/erase de config.
   - `i18n`: nenhuma chave vazia/faltando em nenhum dos 5 idiomas.
2. **e2e de protocolo** (expande o existente): além do atual —
   - **persistência pós-reboot**: derrubar o QEMU e resubir com a MESMA
     flash → mesmo device_id, config preservada, `seq` continua sem duplicar
     leituras no servidor;
   - **queda e recuperação do servidor**: raiznetd fora → buffer acumula →
     volta → tudo entregue;
   - **force-read** via API dispara leitura imediata.
3. **Web UI local**:
   - `emu/webui-e2e.sh` (curl+jq): as 16 rotas — shapes de `/api/status` e
     `/api/telemetry`, `/api/ph/manual` (200/400 e efeito na telemetria),
     `/config/save` efetivando, `/reset/factory/confirm` (reset + device
     volta), `/reset/wifi`, páginas e assets 200.
   - **Playwright headless** (decisão do Yan: incluir): `emu/webui/` com
     Node/`@playwright/test` (chromium baixado pelo Playwright; não há
     browser no sistema), specs de interação real: dashboard renderiza
     valores de telemetria, navegação entre páginas, formulário de config
     salva, toggle de tema persiste.

**Runner**: `emu/test-all.sh` roda 1→2→3 e para no primeiro erro. É o
comando canônico para agentes antes de declarar trabalho pronto.

**Infra compartilhada**: `emu/lib.sh` com as funções hoje duplicadas entre
run.sh/e2e.sh (download do QEMU, preflight libslirp, start/stop do raiznetd,
start/stop do QEMU, cleanup) — os scripts existentes passam a consumi-la.

## Fora de escopo (gaps documentados no emu/README)

- Portal captivo Wi-Fi (WiFiManager), leitura de QR por câmera, OTA,
  botão BOOT e LEDs físicos → exigem a placa real.
- CI (GitHub Actions) — tudo fica scriptável para entrar depois.
- Lado servidor: o raiznetd tem sua própria suíte (corpus) no repo Raiznet.

## Critérios de aceite

1. `pio test -e qemu` roda no QEMU e reporta via runner do PlatformIO;
   falha de assert = exit ≠ 0.
2. Cobertura unit mínima: identity, telemetry-format, buffer, storage, i18n.
3. Cenário de reboot prova persistência de identidade/config e continuidade
   de `seq`.
4. `emu/webui-e2e.sh` cobre TODAS as rotas registradas em
   `http_local.cpp:1721-1751`.
5. Playwright roda headless contra o emulador vivo e passa.
6. `emu/test-all.sh` orquestra tudo; qualquer falha → exit ≠ 0.
7. Env de produção `esp32doit-devkit-v1` compila inalterado; o refactor de
   `format.{h,cpp}` não muda nenhum byte do comportamento em produção.
