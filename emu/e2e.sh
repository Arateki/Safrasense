#!/usr/bin/env bash
# Teste e2e: firmware emulado (QEMU) ↔ raiznetd.
#   Fase 1: banco limpo → device registra e telemetria chega nos dois listeners.
#   Fase 2: banco recriado → firmware re-registra sozinho (fix do 207).
set -euo pipefail
cd "$(dirname "$0")/.."

RAIZNET_DIR="${RAIZNET_DIR:-../Raiznet-rust}"
QEMU_URL="https://github.com/espressif/qemu/releases/download/esp-develop-9.0.0-20240606/qemu-xtensa-softmmu-esp_develop_9.0.0_20240606-x86_64-linux-gnu.tar.xz"
QEMU_BIN="emu/.qemu/qemu/bin/qemu-system-xtensa"
DATA_DIR=""
EMU_LOG=""
RAIZNETD_PID=""
EMU_PID=""

start_raiznetd() {
  RAIZNET_DATA_DIR="$DATA_DIR" "$RAIZNET_DIR/target/debug/raiznetd" &
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
  [ -n "$EMU_PID" ] && kill "$EMU_PID" 2>/dev/null || true
  rm -rf "$DATA_DIR"
  echo "e2e: log do emulador em $EMU_LOG"
}
trap cleanup EXIT INT TERM

if [ ! -x "$QEMU_BIN" ]; then
  echo "[e2e] Baixando QEMU da Espressif (uma vez)..."
  mkdir -p emu/.qemu
  curl -L "$QEMU_URL" | tar -xJ -C emu/.qemu
fi

if ldd "$QEMU_BIN" 2>/dev/null | grep -q "not found"; then
  echo "[emu] Dependências de sistema faltando para o QEMU:" >&2
  ldd "$QEMU_BIN" | grep "not found" >&2
  echo "[emu] Em Fedora: sudo dnf install libslirp" >&2
  exit 1
fi

export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e qemu >/dev/null
rm -f emu/flash.bin
emu/mkflash.sh >/dev/null
cargo build -q -p raiznetd --manifest-path "$RAIZNET_DIR/Cargo.toml"

DATA_DIR="$(mktemp -d /tmp/raiznetd-e2e.XXXXXX)"
EMU_LOG="$(mktemp /tmp/emu-e2e.XXXXXX.log)"

start_raiznetd
"$QEMU_BIN" -nographic -machine esp32 \
  -drive file=emu/flash.bin,if=mtd,format=raw \
  -nic user,model=open_eth \
  -serial file:"$EMU_LOG" >/dev/null 2>&1 &
EMU_PID=$!

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
