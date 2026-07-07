#!/usr/bin/env bash
# Suíte completa anti-regressão. Exit ≠ 0 em qualquer falha.
# É o comando canônico antes de declarar qualquer trabalho pronto.
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$HOME/.platformio/penv/bin:$PATH"

echo "══ 1/5 unit (pio test no QEMU)"; pio test -e qemu
echo "══ 2/5 e2e protocolo";           emu/e2e.sh
echo "══ 3/5 e2e ciclo de vida";       emu/e2e-lifecycle.sh
echo "══ 4/5 web UI (curl)";           emu/webui-e2e.sh
echo "══ 5/5 web UI (Playwright)";     emu/webui-playwright.sh
echo "✔ test-all: tudo verde"
