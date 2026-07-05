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
`python3`, `libslirp` (rede do QEMU — em muitas distros já vem instalada
como dependência de outros pacotes; senão, instale via seu gerenciador de
pacotes, ex. `pacman -S libslirp` / `apt install libslirp0` / `dnf install
libslirp`).

## Solução de problemas

- **`idf-component-manager` falha no PlatformIO (Python 3.14+)**: o
  espidf 4.4.7 fixa uma versão incompatível. Corrija instalando
  `idf-component-manager~=1.2` no venv
  `~/.platformio/penv/.espidf-4.4.7` e criando o arquivo vazio
  `~/.platformio/packages/framework-espidf/.pio_skip_pypackages`.
