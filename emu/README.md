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

## Testes

**`emu/test-all.sh`** é o comando canônico: roda as 5 camadas abaixo em
sequência e falha (exit ≠ 0) em qualquer regressão. Rode antes de declarar
qualquer trabalho pronto.

| Camada | Comando | O que cobre |
|---|---|---|
| Unit no QEMU | `pio test -e qemu` | Unity dentro do emulador: identity (mnemonic, goldens de chave/assinatura), i18n, formato de telemetria, buffer, storage/NVS |
| E2E protocolo | `emu/e2e.sh` | Registro, telemetria nos dois listeners, re-registro após wipe |
| E2E ciclo de vida | `emu/e2e-lifecycle.sh` | Reboot com persistência de config/seq, queda do servidor com reentrega do buffer, force-read |
| Web UI (curl) | `emu/webui-e2e.sh` | As 16 rotas do servidor local: páginas, assets, APIs JSON, config/save, reset wifi/factory |
| Web UI (browser) | `emu/webui-playwright.sh` | JavaScript do front no chromium headless: dashboard com valores sintéticos, navegação, formulário de config, toggle de tema |

O Playwright vive em `emu/webui/` (Node; `npm install` +
`npx playwright install chromium` na primeira execução — o wrapper instala
sozinho se faltar).

**Goldens**: valores pinados (pubkeys, assinaturas, strings de telemetria)
foram capturados de uma execução real; cada teste documenta como regenerar.

**Fora do alcance do emulador** (exigem placa real): portal Wi-Fi/captive
portal, pareamento por QR com câmera, OTA, botão físico e LEDs.

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
