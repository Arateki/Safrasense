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
