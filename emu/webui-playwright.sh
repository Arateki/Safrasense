#!/usr/bin/env bash
# Testes de browser real (Playwright/chromium headless) na web UI local do
# firmware. Sobe a mesma stack do webui-e2e.sh (raiznetd + QEMU com hostfwd
# 8180) e roda `npm test` em emu/webui/. Requer `npm install` e
# `npx playwright install chromium` já feitos (o script instala se faltar).
set -euo pipefail
source "$(dirname "$0")/lib.sh"
cd "$(dirname "$0")/.."

DATA_DIR=""
EMU_LOG=""
UI_PORT=8180
NIC_SUFFIX=",hostfwd=tcp:127.0.0.1:${UI_PORT}-:80"

cleanup() {
  emu_cleanup_default
  echo "webui-playwright: log do emulador em $EMU_LOG"
}
trap cleanup EXIT INT TERM

emu_ensure_qemu webui-playwright

export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e qemu >/dev/null
rm -f emu/flash.bin
emu/mkflash.sh >/dev/null
emu_build_raiznetd

if [ ! -d emu/webui/node_modules ]; then
  echo "webui-playwright: instalando dependências (primeira execução)"
  (cd emu/webui && npm install && npx playwright install chromium)
fi

DATA_DIR="$(mktemp -d /tmp/raiznetd-webui-pw.XXXXXX)"
EMU_LOG="$(mktemp /tmp/emu-webui-pw.XXXXXX.log)"

emu_start_raiznetd "$DATA_DIR" || { echo "FALHOU: raiznetd não subiu"; exit 1; }
emu_start_qemu emu/flash.bin "$EMU_LOG" "$NIC_SUFFIX"

echo "webui-playwright: aguardando UI local subir na porta $UI_PORT"
DEADLINE=$(( $(date +%s) + 180 ))
until curl -sf "http://127.0.0.1:${UI_PORT}/api/status" >/dev/null 2>&1; do
  [ "$(date +%s)" -lt "$DEADLINE" ] || { echo "FALHOU: UI local não respondeu após boot"; exit 1; }
  sleep 2
done

echo "webui-playwright: rodando os specs"
(cd emu/webui && npm test)

echo "webui-playwright OK"
