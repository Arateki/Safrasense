#!/usr/bin/env bash
# Dev interativo: compila o firmware, sobe o raiznetd local e roda o QEMU.
# Tudo local: sem token, sem nuvem. Host visto do firmware = 10.0.2.2.
set -euo pipefail
cd "$(dirname "$0")/.."

RAIZNET_DIR="${RAIZNET_DIR:-../Raiznet-rust}"
QEMU_URL="https://github.com/espressif/qemu/releases/download/esp-develop-9.0.0-20240606/qemu-xtensa-softmmu-esp_develop_9.0.0_20240606-x86_64-linux-gnu.tar.xz"
QEMU_BIN="emu/.qemu/qemu/bin/qemu-system-xtensa"

DATA_DIR_CREATED=0
if [ -z "${DATA_DIR:-}" ]; then
  DATA_DIR="$(mktemp -d /tmp/raiznetd-emu.XXXXXX)"
  DATA_DIR_CREATED=1
fi

if [ ! -x "$QEMU_BIN" ]; then
  echo "[run] Baixando QEMU da Espressif (uma vez)..."
  mkdir -p emu/.qemu
  curl -L "$QEMU_URL" | tar -xJ -C emu/.qemu
fi

export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e qemu
if [ ! -f emu/flash.bin ] || [ .pio/build/qemu/firmware.bin -nt emu/flash.bin ]; then
  emu/mkflash.sh
fi

echo "[run] raiznetd: dados em $DATA_DIR, portas 3000 (público) / 3001 (local)"
cargo build -q -p raiznetd --manifest-path "$RAIZNET_DIR/Cargo.toml"
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

# UI local do firmware: http://localhost:8180 (hostfwd → porta 80 do device).
"$QEMU_BIN" -nographic -machine esp32 \
  -drive file=emu/flash.bin,if=mtd,format=raw \
  -nic user,model=open_eth,hostfwd=tcp:127.0.0.1:8180-:80 \
  -serial mon:stdio
