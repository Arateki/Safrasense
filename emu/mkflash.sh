#!/usr/bin/env bash
# Monta a imagem de flash 4MB para o QEMU a partir do build do env qemu.
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD=.pio/build/qemu
ESPTOOL="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
OUT=emu/flash.bin

# O build híbrido gera bootloader.bin/partitions.bin/firmware.bin; o offset
# do ota_data (0xe000) só entra se o arquivo existir no build.
ARGS=(0x1000 "$BUILD/bootloader.bin" 0x8000 "$BUILD/partitions.bin" 0x10000 "$BUILD/firmware.bin")
for f in ota_data_initial.bin boot_app0.bin; do
  if [ -f "$BUILD/$f" ]; then ARGS=(0xe000 "$BUILD/$f" "${ARGS[@]}"); break; fi
done

python3 "$ESPTOOL" --chip esp32 merge_bin -o "$OUT" \
  --flash_mode dio --flash_freq 40m --flash_size 4MB --fill-flash-size 4MB \
  "${ARGS[@]}"
echo "[mkflash] $OUT pronto ($(stat -c%s "$OUT") bytes)"
