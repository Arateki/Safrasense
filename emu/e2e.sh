#!/usr/bin/env bash
# Teste e2e: firmware emulado (QEMU) ↔ raiznetd.
#   Fase 1: banco limpo → device registra e telemetria chega nos dois listeners.
#   Fase 2: banco recriado → firmware re-registra sozinho (fix do 207).
set -euo pipefail
source "$(dirname "$0")/lib.sh"
cd "$(dirname "$0")/.."

DATA_DIR=""
EMU_LOG=""

cleanup() {
  emu_cleanup_default
  echo "e2e: log do emulador em $EMU_LOG"
}
trap cleanup EXIT INT TERM

emu_ensure_qemu e2e

export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e qemu >/dev/null
rm -f emu/flash.bin
emu/mkflash.sh >/dev/null
emu_build_raiznetd

DATA_DIR="$(mktemp -d /tmp/raiznetd-e2e.XXXXXX)"
EMU_LOG="$(mktemp /tmp/emu-e2e.XXXXXX.log)"

emu_start_raiznetd "$DATA_DIR" || { echo "e2e: raiznetd não subiu"; exit 1; }
emu_start_qemu emu/flash.bin "$EMU_LOG"

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
emu_stop_raiznetd
rm -rf "$DATA_DIR"
mkdir -p "$DATA_DIR"
emu_start_raiznetd "$DATA_DIR" || { echo "e2e: raiznetd não subiu"; exit 1; }
wait_readings 3000 1 120 || { echo "FALHOU: device não se re-registrou após wipe do banco"; exit 1; }

echo "e2e OK"
