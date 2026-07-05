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
