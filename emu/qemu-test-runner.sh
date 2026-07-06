#!/usr/bin/env bash
# Runner do `pio test -e qemu`: monta a flash do firmware de TESTE e roda o
# QEMU com o serial no stdout — o PlatformIO parseia o output do Unity e
# encerra este processo quando o relatório termina.
#
# Duas armadilhas observadas (ver task-1-suite-report.md):
#   1. `-serial stdio -monitor none` não produz NENHUM byte de saída (o boot
#      log do ESP32 simplesmente não aparece); o combo que funciona é
#      `-serial mon:stdio` (serial + monitor multiplexados no stdio).
#   2. `-no-reboot` evita reboot automático, mas o firmware de teste termina
#      em `loop() {}` (ocioso) — o QEMU nunca sai sozinho. O parser do PIO
#      (NativeTestOutputReader) tolera isso (só espera 5s após ver o
#      sumário do Unity e segue em frente mesmo com o processo ainda vivo),
#      mas isso deixaria um `qemu-system-xtensa` órfão a cada `pio test`.
#      Por isso este wrapper mata o QEMU assim que vê a linha de sumário do
#      Unity ("N Tests N Failures N Ignored"), e sempre sai com status 0 —
#      o resultado real (pass/fail) já foi extraído do texto pelo PIO antes
#      disso, e um exit code não-zero aqui faria o PIO derrubar o relatório
#      já OK como "ERRORED" (ver NativeTestOutputReader.raise_for_status).
set -uo pipefail
cd "$(dirname "$0")/.."
BUILD="$1"
FLASH="$(mktemp /tmp/safra-test-flash.XXXXXX.bin)"
WATCHDOG_PID=""
QEMU_PID=""

cleanup() {
  if [ -n "${QEMU_PID:-}" ] && kill -0 "$QEMU_PID" 2>/dev/null; then
    kill "$QEMU_PID" 2>/dev/null || true
    sleep 1
    kill -9 "$QEMU_PID" 2>/dev/null || true
  fi
  [ -n "$WATCHDOG_PID" ] && kill "$WATCHDOG_PID" 2>/dev/null
  rm -f "$FLASH"
}
trap cleanup EXIT INT TERM
emu/mkflash.sh "$BUILD" "$FLASH" >&2 || exit 1

# coproc para termos o PID do QEMU e poder matá-lo ao ver o fim do relatório.
coproc QEMU {
  exec emu/.qemu/qemu/bin/qemu-system-xtensa -nographic -machine esp32 \
    -drive file="$FLASH",if=mtd,format=raw \
    -serial mon:stdio -no-reboot
}

# Rede de segurança: se o firmware travar antes do UNITY_END (nunca imprime o
# sumário), o QEMU nunca sairia sozinho (ver nota acima) e este script ficaria
# preso no `read` para sempre. Mata o QEMU depois de um tempo generoso.
( sleep 60; kill "$QEMU_PID" 2>/dev/null ) &
WATCHDOG_PID=$!

while IFS= read -r line <&"${QEMU[0]}"; do
  printf '%s\n' "$line"
  if [[ "$line" == *Tests*Failures*Ignored* ]]; then
    sleep 0.3  # dá tempo da linha final "OK"/"FAIL" ser emitida também
    read -r -t 1 tail_line <&"${QEMU[0]}" && printf '%s\n' "$tail_line"
    kill "$QEMU_PID" 2>/dev/null
    break
  fi
done

wait "$QEMU_PID" 2>/dev/null
exit 0
