#!/usr/bin/env bash
# Dev interativo: compila o firmware, sobe o raiznetd local e roda o QEMU.
# Tudo local: sem token, sem nuvem. Host visto do firmware = 10.0.2.2.
set -euo pipefail
source "$(dirname "$0")/lib.sh"
cd "$(dirname "$0")/.."

DATA_DIR_CREATED=0
DATA_DIR="${DATA_DIR:-}"

cleanup() {
  emu_cleanup_default
}
trap cleanup EXIT INT TERM

if [ -z "$DATA_DIR" ]; then
  DATA_DIR="$(mktemp -d /tmp/raiznetd-emu.XXXXXX)"
  DATA_DIR_CREATED=1
fi

emu_ensure_qemu run

export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e qemu
if [ ! -f emu/flash.bin ] || [ .pio/build/qemu/firmware.bin -nt emu/flash.bin ]; then
  emu/mkflash.sh
fi

echo "[run] raiznetd: dados em $DATA_DIR, portas 3000 (público) / 3001 (local)"
emu_build_raiznetd
emu_start_raiznetd "$DATA_DIR" || { echo "[run] raiznetd não subiu"; exit 1; }

# UI local do firmware: http://localhost:8180 (hostfwd → porta 80 do device).
"$QEMU_BIN" -nographic -machine esp32 \
  -drive file=emu/flash.bin,if=mtd,format=raw \
  -nic user,model=open_eth,hostfwd=tcp:127.0.0.1:8180-:80 \
  -serial mon:stdio
