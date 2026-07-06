#!/usr/bin/env bash
# Infra comum aos scripts de emu/: download+preflight do QEMU, build/start/stop
# do raiznetd, start/stop do QEMU em background e um cleanup padrão para trap.
#
# Uso: source "$(dirname "$0")/lib.sh"  (antes do `cd` para a raiz do repo,
# assim o path relativo do próprio lib.sh continua correto).
#
# Variáveis globais usadas/definidas pelas funções abaixo:
#   RAIZNET_DIR       - checkout do Raiznet-rust (default ../Raiznet-rust,
#                       sobrescrevível via env; caminho relativo à raiz do
#                       repo, resolvido só quando efetivamente usado).
#   QEMU_URL          - tarball pinado do QEMU da Espressif.
#   QEMU_BIN          - path do binário qemu-system-xtensa após extração.
#   DATA_DIR          - diretório de dados do raiznetd (consumido por
#                       emu_start_raiznetd e emu_cleanup_default).
#   DATA_DIR_CREATED  - "1" (default se não setada) quando DATA_DIR foi
#                       criada pelo próprio script e deve ser removida no
#                       cleanup; "0" para preservar um DATA_DIR fornecido
#                       pelo usuário.
#   RAIZNETD_PID      - setada por emu_start_raiznetd, limpa por
#                       emu_stop_raiznetd.
#   EMU_PID           - setada por emu_start_qemu, limpa por emu_stop_qemu.

RAIZNET_DIR="${RAIZNET_DIR:-../Raiznet-rust}"
QEMU_URL="https://github.com/espressif/qemu/releases/download/esp-develop-9.0.0-20240606/qemu-xtensa-softmmu-esp_develop_9.0.0_20240606-x86_64-linux-gnu.tar.xz"
QEMU_BIN="emu/.qemu/qemu/bin/qemu-system-xtensa"

RAIZNETD_PID="${RAIZNETD_PID:-}"
EMU_PID="${EMU_PID:-}"

# emu_ensure_qemu [PREFIX]
# Baixa o QEMU pinado se ainda não estiver extraído e roda o preflight de
# libslirp. PREFIX (default "emu") é usado só na mensagem de download, para
# cada script poder manter seu próprio prefixo de log ("[run]", "[e2e]", ...).
emu_ensure_qemu() {
  local prefix="${1:-emu}"

  if [ ! -x "$QEMU_BIN" ]; then
    echo "[$prefix] Baixando QEMU da Espressif (uma vez)..."
    mkdir -p emu/.qemu
    curl -L "$QEMU_URL" | tar -xJ -C emu/.qemu
  fi

  if ldd "$QEMU_BIN" 2>/dev/null | grep -q "not found"; then
    echo "[emu] Dependências de sistema faltando para o QEMU:" >&2
    ldd "$QEMU_BIN" | grep "not found" >&2
    echo "[emu] No Arch: sudo pacman -S libslirp (Fedora: dnf install libslirp)" >&2
    exit 1
  fi
}

# emu_build_raiznetd
# Compila o binário debug do raiznetd.
emu_build_raiznetd() {
  cargo build -q -p raiznetd --manifest-path "$RAIZNET_DIR/Cargo.toml"
}

# emu_start_raiznetd DATA_DIR
# Sobe o raiznetd em background com RAIZNET_DATA_DIR=DATA_DIR, seta
# RAIZNETD_PID e espera até 30s pelo /health. Retorna 1 (sem imprimir nada)
# se o /health nunca respondeu — quem chama decide a mensagem/exit.
emu_start_raiznetd() {
  local data_dir="$1"
  RAIZNET_DATA_DIR="$data_dir" "$RAIZNET_DIR/target/debug/raiznetd" &
  RAIZNETD_PID=$!

  for _ in $(seq 1 30); do
    curl -sf http://127.0.0.1:3000/health >/dev/null 2>&1 && return 0
    sleep 1
  done
  return 1
}

# emu_stop_raiznetd
# Mata o raiznetd apontado por RAIZNETD_PID (se houver) e limpa a variável.
emu_stop_raiznetd() {
  [ -n "$RAIZNETD_PID" ] && kill "$RAIZNETD_PID" 2>/dev/null || true
  wait "$RAIZNETD_PID" 2>/dev/null || true
  RAIZNETD_PID=""
}

# emu_start_qemu FLASH LOG [NIC_SUFFIX]
# Sobe o QEMU em background (sem tela, sem stdio interativo), logando a
# serial em LOG. NIC_SUFFIX (se houver) é concatenado dentro do PRÓPRIO
# valor de -nic (ex.: ",hostfwd=tcp:127.0.0.1:8180-:80"), nunca como argv
# separado — um segundo "-nic user,..." criaria uma segunda interface de
# rede e o hostfwd não funcionaria. Seta EMU_PID.
emu_start_qemu() {
  local flash="$1" log="$2" nic_suffix="${3:-}"
  "$QEMU_BIN" -nographic -machine esp32 \
    -drive file="$flash",if=mtd,format=raw \
    -nic "user,model=open_eth${nic_suffix}" \
    -serial file:"$log" >/dev/null 2>&1 &
  EMU_PID=$!
}

# emu_stop_qemu
# Mata o QEMU apontado por EMU_PID (se houver) e limpa a variável.
emu_stop_qemu() {
  [ -n "$EMU_PID" ] && kill "$EMU_PID" 2>/dev/null || true
  EMU_PID=""
}

# emu_cleanup_default
# Cleanup padrão para usar direto num `trap ... EXIT INT TERM`: para
# raiznetd e QEMU (se estiverem rodando) e remove DATA_DIR, a menos que
# DATA_DIR_CREATED="0" (preserva um DATA_DIR fornecido pelo usuário).
# Scripts com mensagens extras (ex.: e2e's "log do emulador em ...") devem
# envolver esta função na própria cleanup() e chamá-la primeiro.
emu_cleanup_default() {
  emu_stop_raiznetd
  emu_stop_qemu
  if [ -n "${DATA_DIR:-}" ] && [ "${DATA_DIR_CREATED:-1}" = 1 ]; then
    rm -rf "$DATA_DIR"
  fi
}
