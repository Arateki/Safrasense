#!/usr/bin/env bash
# Dev interativo: compila o firmware, sobe o raiznetd local e roda o emulador.
# Pré-requisitos: pio, cargo, curl, wokwi-cli autenticado (WOKWI_CLI_TOKEN).
set -euo pipefail
cd "$(dirname "$0")/.."

RAIZNET_DIR="${RAIZNET_DIR:-../Raiznet-rust}"
DATA_DIR_CREATED=0
if [ -z "${DATA_DIR:-}" ]; then
  DATA_DIR="$(mktemp -d /tmp/raiznetd-emu.XXXXXX)"
  DATA_DIR_CREATED=1
fi

pio run -e wokwi
cargo build -q -p raiznetd --manifest-path "$RAIZNET_DIR/Cargo.toml"

echo "[run] raiznetd: dados em $DATA_DIR, portas 3000 (público) / 3001 (local)"
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

wokwi-cli --interactive --timeout 0 emu
