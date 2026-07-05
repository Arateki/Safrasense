# Suíte de testes anti-regressão — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `emu/test-all.sh` exercita todas as superfícies emuláveis do firmware (unit no QEMU, e2e de protocolo, Web UI via curl e Playwright) e falha com exit ≠ 0 em qualquer regressão.

**Architecture:** Camada 1 = testes Unity rodando NO QEMU via `pio test -e qemu` + `test_testing_command` (wrapper que monta a flash de teste e roda o QEMU com serial no stdout). Camada 2 = novos cenários e2e (reboot/persistência, queda do servidor, force-read). Camada 3 = `webui-e2e.sh` (curl nas 16 rotas) + Playwright headless em `emu/webui/`. Infra comum extraída para `emu/lib.sh`.

**Tech Stack:** PlatformIO Unity, QEMU Espressif (já em `emu/.qemu/`), bash+curl+jq, Node 26 + @playwright/test (chromium baixado pelo Playwright — não há browser no sistema; máquina é Arch).

## Global Constraints

- Produção (`esp32doit-devkit-v1`) compila inalterada; refactors não mudam comportamento em produção.
- `pio` em `$HOME/.platformio/penv/bin`; raiznetd via binário direto (`cargo build -p raiznetd --manifest-path ../Raiznet-rust/Cargo.toml` + `target/debug/raiznetd`, `RAIZNET_DATA_DIR` absoluto, portas 3000/3001).
- QEMU: `emu/.qemu/qemu/bin/qemu-system-xtensa`; host visto do guest = `10.0.2.2`; UI do firmware via `hostfwd tcp:127.0.0.1:8180-:80`.
- Golden values (pubkeys, assinaturas, raw strings): capturados UMA vez da execução real e pinados no teste (regression pinning); documentar no próprio teste como regenerar.
- Rotas da Web UI: a lista canônica é `src/http_local/http_local.cpp:1721-1751` (16 rotas).
- Commits sem menção a Claude e sem trailer Co-Authored-By.

---

### Task 1: Spike — `pio test -e qemu` rodando Unity no QEMU

**Files:**
- Modify: `platformio.ini` (`[env:qemu]`)
- Modify: `src/main.cpp` (guard de teste)
- Modify: `emu/mkflash.sh` (parametrizar build dir/saída)
- Create: `emu/qemu-test-runner.sh`
- Create: `test/test_smoke/test_main.cpp`

**Interfaces:**
- Produces: `pio test -e qemu` funcional — Tasks 2–3 só adicionam pastas `test/test_*`. `emu/mkflash.sh [BUILD_DIR] [OUT]` (defaults atuais preservados) — consumido também pelo runner.

- [ ] **Step 1: Config de teste no `[env:qemu]`** — acrescentar ao bloco:

```ini
test_framework = unity
test_build_src = yes
test_testing_command =
    bash
    ${platformio.workspace_dir}/../emu/qemu-test-runner.sh
    ${platformio.build_dir}/${this.__env__}
```

- [ ] **Step 2: Guard no `src/main.cpp`** — envolver o arquivo inteiro (após os includes) com `#ifndef PIO_UNIT_TESTING` ... `#endif` (o PIO define esse macro em builds de teste; evita conflito de `setup`/`loop` com o firmware de teste).

- [ ] **Step 3: Parametrizar `emu/mkflash.sh`** — aceitar `BUILD` como `$1` (default `.pio/build/qemu`) e `OUT` como `$2` (default `emu/flash.bin`); resto igual.

- [ ] **Step 4: Criar `emu/qemu-test-runner.sh`** (chmod +x):

```bash
#!/usr/bin/env bash
# Runner do `pio test -e qemu`: monta a flash do firmware de TESTE e roda o
# QEMU com o serial no stdout — o PlatformIO parseia o output do Unity e
# encerra este processo quando o relatório termina.
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD="$1"
FLASH="$(mktemp /tmp/safra-test-flash.XXXXXX.bin)"
trap 'rm -f "$FLASH"' EXIT
emu/mkflash.sh "$BUILD" "$FLASH" >&2
exec emu/.qemu/qemu/bin/qemu-system-xtensa -nographic -machine esp32 \
  -drive file="$FLASH",if=mtd,format=raw \
  -monitor none -serial stdio -no-reboot
```

Nota: se o parser do PIO não matar o QEMU ao fim (teste trava após o sumário), envolver com timeout e detectar o terminador do Unity no wrapper — resolver pelo comportamento observado e registrar no report.

- [ ] **Step 5: Teste fumaça `test/test_smoke/test_main.cpp`**:

```cpp
#include <Arduino.h>
#include <unity.h>

void test_sanity() { TEST_ASSERT_EQUAL(4, 2 + 2); }
void test_nvs_disponivel() {
  // Prova que o ambiente QEMU dá NVS real aos testes.
  #include <Preferences.h>
  Preferences p;
  TEST_ASSERT_TRUE(p.begin("t_smoke", false));
  p.putInt("x", 42);
  TEST_ASSERT_EQUAL(42, p.getInt("x", 0));
  p.end();
}

void setup() {
  delay(200);
  UNITY_BEGIN();
  RUN_TEST(test_sanity);
  RUN_TEST(test_nvs_disponivel);
  UNITY_END();
}
void loop() {}
```

(Se `#include` dentro de função não compilar, mover para o topo — óbvio, mas o executor decide pela dica do compilador.)

- [ ] **Step 6: Rodar** — `export PATH="$HOME/.platformio/penv/bin:$PATH" && pio test -e qemu`
Expected: `2 Tests 0 Failures` e exit 0. Depois: `pio run -e qemu && pio run -e esp32doit-devkit-v1` SUCCESS (guard do main não quebrou nada).

- [ ] **Step 7: Commit** — `git add platformio.ini src/main.cpp emu/mkflash.sh emu/qemu-test-runner.sh test/test_smoke/ && git commit -m "test: pio test -e qemu rodando Unity dentro do QEMU"`

---

### Task 2: Testes de identity e i18n

**Files:**
- Create: `test/test_identity/test_main.cpp`
- Create: `test/test_i18n/test_main.cpp`

**Interfaces:**
- Consumes: infra da Task 1; API de `src/identity/identity.h` e `src/i18n/i18n.h` (ler os headers antes de escrever).

- [ ] **Step 1: `test/test_identity/test_main.cpp`** — casos mínimos (Unity, mesma estrutura do smoke):
  - `validateMnemonicForLanguage`: 12 palavras válidas EN ("abandon ... about") → true; 11 palavras → false com erro `word_count`; palavra fora da wordlist → `wordlist`; normalização (espaços duplos, maiúsculas) aceita.
  - `analyzeMnemonicForLanguage`: prefixo na última palavra → `partial` com sugestões ≥1.
  - `importOwnerIdentity` com mnemonic fixa → `owner_public_key_hex` GOLDEN (capturar na primeira execução, pinar, comentar como regenerar).
  - `signMessage` com identidade de seed fixa sobre msg fixa → assinatura GOLDEN de 128 hex chars.
  - `saveIdentity`/`loadOrCreateIdentity`: no env de teste o flag `QEMU_EMULATOR` está ativo → `loadOrCreateIdentity` retorna a identidade fixa; assert `public_key_hex` GOLDEN (o mesmo device_id que aparece no raiznetd).
- [ ] **Step 2: `test/test_i18n/test_main.cpp`** — para cada idioma (5) e cada chave usada pelo firmware (extrair a lista real de `src/i18n/i18n.h`/`.cpp`): `t(key, lang)` não vazio. Se a lista de chaves não for enumerável programaticamente, gerar um array estático no teste com TODAS as chaves encontradas via grep e afirmar não-vazio (o teste então também documenta o inventário).
- [ ] **Step 3: Rodar** — `pio test -e qemu` → todos os testes (smoke+identity+i18n) verdes.
- [ ] **Step 4: Commit** — `git add test/ && git commit -m "test: identity (mnemonic, goldens de chave/assinatura) e i18n no QEMU"`

---

### Task 3: Refactor format + testes de telemetry/buffer/storage

**Files:**
- Create: `src/telemetry/format.h`, `src/telemetry/format.cpp`
- Modify: `src/telemetry/telemetry.cpp` (usar as funções extraídas)
- Create: `test/test_telemetry/test_main.cpp`
- Create: `test/test_storage/test_main.cpp`

**Interfaces:**
- Produces: `String telemetryBuildRaw(const TelemetryEntry&, const String& devicePubkeyHex)` e `String telemetryBuildJson(const TelemetryEntry&, const String& devicePubkeyHex, const String& raw, const String& sigHex)` em `format.h` — código MOVIDO de `telemetry.cpp:39-100` (buildRaw/buildJson/fieldValue/u64ToString/toHex), sem nenhuma mudança de lógica; `telemetry.cpp` chama as novas funções passando `gId->public_key_hex`.

- [ ] **Step 1: Extrair `format.{h,cpp}`** — mover as funções estáticas citadas; produção idêntica (diff de comportamento zero; conferir que `pio run -e esp32doit-devkit-v1` gera o MESMO tamanho de binário ± alinhamento).
- [ ] **Step 2: `test/test_telemetry/test_main.cpp`**:
  - Golden do raw: entry {seq=7, ts=1700000000123, ec=1400.4, ph=6.204, water_level=120, temp=24.5, hum=60.2} + pubkey "ab"×32 →
    `"abab...|7|1700000000123|0|ec=1400|ph=6.20|waterLevel=120|tempAmbient=24.50|humidity=60.20"` (string completa no teste).
  - Campos NAN/negativos omitidos: entry só com ph → raw sem ec/waterLevel/etc.
  - JSON: parsear com ArduinoJson e afirmar: `blocks[0].seq == "7"` (STRING), `timestamp` string, `keyVersion == 0`, `ph.plain == 6.2`, `raw` = hex do raw, `signature` ecoada.
  - Buffer (`src/telemetry/buffer.h` — ler o header): add → nextPending(mask) retorna; confirmServer para todos os bits da mask → allDone e sai das pendências; overflow: adicionar TELEMETRY_BUFFER_SIZE+5 e afirmar pendingCount ≤ TELEMETRY_BUFFER_SIZE; seq estritamente crescente entre adds.
- [ ] **Step 3: `test/test_storage/test_main.cpp`** — `eraseConfig()` → `loadConfig()` traz defaults do emulador (EMU_SERVER_*); alterar nome+servidores → `saveConfig` → `loadConfig` devolve igual; `eraseConfig` → defaults de novo.
- [ ] **Step 4: Rodar** — `pio test -e qemu` tudo verde; `pio run -e esp32doit-devkit-v1` SUCCESS.
- [ ] **Step 5: Commit** — `git add src/telemetry/ test/ && git commit -m "test: goldens de formato de telemetria, buffer e storage (refactor format.h)"`

---

### Task 4: `emu/lib.sh` — infra comum dos scripts

**Files:**
- Create: `emu/lib.sh`
- Modify: `emu/run.sh`, `emu/e2e.sh` (consumir a lib)

**Interfaces:**
- Produces (funções bash, todas usando variáveis globais documentadas no topo da lib): `emu_ensure_qemu` (download pinado + preflight libslirp), `emu_build_raiznetd`, `emu_start_raiznetd DATA_DIR` (health-wait; seta `RAIZNETD_PID`), `emu_stop_raiznetd`, `emu_start_qemu FLASH LOG [EXTRA_ARGS...]` (background; seta `EMU_PID`), `emu_stop_qemu`, `emu_cleanup_default` (para trap). Tasks 5–7 consomem.

- [ ] **Step 1: Extrair para `emu/lib.sh`** o código hoje duplicado em run.sh/e2e.sh (QEMU_URL/QEMU_BIN, download, preflight, start/stop raiznetd, cleanup). Manter comportamento byte-idêntico; scripts fazem `source "$(dirname "$0")/lib.sh"`.
- [ ] **Step 2: Verificar** — `bash -n` nos 3; rodar `emu/e2e.sh` completo → `e2e OK` exit 0 (prova que o refactor não quebrou nada).
- [ ] **Step 3: Commit** — `git add emu/ && git commit -m "refactor(emu): lib.sh com a infra comum de raiznetd/QEMU"`

---

### Task 5: e2e de ciclo de vida — reboot, queda do servidor, force-read

**Files:**
- Create: `emu/e2e-lifecycle.sh` (chmod +x)

**Interfaces:**
- Consumes: `emu/lib.sh`. QEMU aqui sobe COM hostfwd 8180 (a API do firmware é usada nos asserts).

- [ ] **Step 1: Criar o script** com esta sequência (helpers `wait_readings`-style como no e2e.sh; falha → exit 1 com mensagem `FALHOU: ...`):
  1. Flash nova (`rm -f` + mkflash), raiznetd novo, QEMU com hostfwd.
  2. Espera ≥2 leituras no servidor; captura `device_id`, `max_seq_1`, e muda o nome do device via `POST /config/save` (form `name=TesteReboot`; conferir efeito em `/api/status`).
  3. **Reboot**: `emu_stop_qemu` (flash preservada) → `emu_start_qemu` mesma flash → espera ≥1 leitura nova.
     Asserts: `GET /v1/devices` continua com **1** device e mesmo id; `max_seq_2 > max_seq_1` (seq continuou, não reiniciou); total de leituras cresceu sem erro; `/api/status` ainda mostra `TesteReboot` (config sobreviveu ao reboot).
  4. **Queda do servidor**: `emu_stop_raiznetd`; espera ~35 s (≥3 ciclos); religa com o MESMO data dir; espera; assert: contagem de leituras salta ≥3 de uma vez (buffer entregue) e continua crescendo.
  5. **Force-read**: captura contagem, `POST /api/force-read`, assert nova leitura no servidor em <10 s.
- [ ] **Step 2: Rodar** até verde; **Step 3: Commit** — `git add emu/e2e-lifecycle.sh && git commit -m "test(emu): e2e de reboot/persistência, queda do servidor e force-read"`

---

### Task 6: `emu/webui-e2e.sh` — curl nas 16 rotas

**Files:**
- Create: `emu/webui-e2e.sh` (chmod +x)

**Interfaces:**
- Consumes: `emu/lib.sh`; stack com hostfwd 8180. Rotas canônicas em `http_local.cpp:1721-1751` (ler o arquivo para os shapes das respostas antes de escrever asserts).

- [ ] **Step 1: Criar o script** — sobe a stack e valida:
  - Páginas 200 + content-type text/html: `/`, `/raiznet`, `/config`, `/docs`, `/reset/wifi`, `/reset/factory`; assets 200: `/local.css`, `/dashboard.js`, `/local-nav.js`; rota inexistente → 404.
  - `/api/status`: JSON com campos esperados (jq: nome, ip, campos de telemetria/servidores conforme o handler).
  - `/api/telemetry`: JSON com a última leitura (valores sintéticos conhecidos: ec 1412, temp 24.5, hum 61.2).
  - `/api/force-read`: `{"ok":true}` e leitura nova no raiznetd em <10 s.
  - `/api/ph/manual`: POST válido (ex.: `ph=6.5`) → 200 `{"ok":true}` e o ph aparece na PRÓXIMA leitura no servidor; POST inválido (`ph=99`) → 400.
  - `/config/save`: POST muda o nome → `/api/status` reflete.
  - `/reset/factory/confirm`: POST → device reinicia (QEMU: aguardar a UI voltar) → nome volta ao default `Safrasense-aqua-*` e device continua o MESMO no servidor (identidade fixa do emulador).
- [ ] **Step 2: Rodar** até verde; **Step 3: Commit** — `git add emu/webui-e2e.sh && git commit -m "test(emu): cobertura curl de todas as rotas da web UI local"`

---

### Task 7: Playwright headless na Web UI

**Files:**
- Create: `emu/webui/package.json`, `emu/webui/playwright.config.ts`, `emu/webui/tests/*.spec.ts`
- Create: `emu/webui-playwright.sh` (chmod +x)
- Modify: `.gitignore` (`emu/webui/node_modules/`, `emu/webui/test-results/`, `emu/webui/playwright-report/`)

**Interfaces:**
- Consumes: `emu/lib.sh` (o wrapper sobe a stack e roda `npm test` dentro de `emu/webui/`).

- [ ] **Step 1: `emu/webui/package.json`** — `private: true`, devDependency `@playwright/test` (latest), scripts `{"test": "playwright test"}`. `npm install` + `npx playwright install chromium` (se faltarem libs de sistema do chromium no Arch, reportar quais — não instalar nada com sudo).
- [ ] **Step 2: `playwright.config.ts`** — baseURL `http://127.0.0.1:8180`, headless, retries 1, timeout generoso (device embarcado: 15 s de navegação), `reporter: 'list'`, sem webServer (a stack vem do wrapper).
- [ ] **Step 3: Specs** (ler o HTML dos handlers para os seletores reais antes de escrever):
  - `dashboard.spec.ts`: `/` carrega, valores sintéticos aparecem (ec/temp/hum), status de servidor visível.
  - `navigation.spec.ts`: links entre `/`, `/raiznet`, `/config`, `/docs` funcionam.
  - `config.spec.ts`: formulário de config salva um nome novo e ele aparece após reload.
  - `theme.spec.ts`: toggle de tema alterna `data-theme` e persiste após reload.
- [ ] **Step 4: `emu/webui-playwright.sh`** — source lib.sh, sobe stack (flash nova), espera UI responder em 8180, `cd emu/webui && npm test`, propaga exit code, cleanup via trap.
- [ ] **Step 5: Rodar** até verde; **Step 6: Commit** — `git add emu/webui emu/webui-playwright.sh .gitignore && git commit -m "test(emu): Playwright headless na web UI local"`

---

### Task 8: `emu/test-all.sh` + README + verificação final

**Files:**
- Create: `emu/test-all.sh` (chmod +x)
- Modify: `emu/README.md`

- [ ] **Step 1: `emu/test-all.sh`**:

```bash
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
```

- [ ] **Step 2: README** — seção "Testes" com o test-all, as camadas, e os gaps fora do emulador (portal Wi-Fi, QR por câmera, OTA, botão/LEDs → placa real).
- [ ] **Step 3: Verificação final** — `emu/test-all.sh` completo verde; `pio run -e esp32doit-devkit-v1` SUCCESS.
- [ ] **Step 4: Commit** — `git add emu/ && git commit -m "test(emu): runner test-all e documentação da suíte"`

---

## Self-Review (feita na escrita)

- **Cobertura da spec:** camada 1 (T1–T3), camada 2 (T5), camada 3 curl (T6) e Playwright (T7), lib comum (T4), runner+gaps (T8); critérios 1–7 todos mapeados. Produção intocada verificada em T1/T3/T8.
- **Placeholders:** nenhum "TBD"; goldens são capturados-e-pinados por definição (documentado na Global Constraints); pontos dependentes de inspeção (headers de buffer/i18n, shapes dos handlers, seletores do HTML) têm instrução explícita de ler o arquivo-fonte antes de escrever o teste.
- **Consistência:** `emu_start_qemu FLASH LOG [EXTRA]` definida em T4 e usada em T5–T7; `mkflash.sh [BUILD] [OUT]` parametrizado em T1 e usado pelo runner; `test_build_src = yes` + guard `PIO_UNIT_TESTING` em T1 cobrem todos os `test/test_*`.
