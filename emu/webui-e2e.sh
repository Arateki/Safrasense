#!/usr/bin/env bash
# Teste e2e da web UI local do firmware (porta 8180 via hostfwd): cobre via
# curl as 16 rotas registradas em src/http_local/http_local.cpp (15
# server.on(...) + o handler onNotFound).
#   Fase 1: páginas/assets estáticos (200 + content-type) e rota 404.
#   Fase 2: /api/status — shape do JSON.
#   Fase 3: /api/telemetry — valores sintéticos conhecidos do QEMU.
#   Fase 4: /api/force-read — nova leitura no raiznetd em <10s.
#   Fase 5: /api/ph/manual — válido (200 + aparece no raiznetd) e inválido (400).
#   Fase 6: /config/save — muda o nome, refletido em /api/status.
#   Fase 7: /reset/wifi — 200 + text/html (no QEMU o firmware pula as
#           chamadas de Wi-Fi — sem rádio — e a UI segue no ar).
#   Fase 8 (LAST): /reset/factory/confirm — erase de config+identidade,
#           power-cycle do QEMU (o reboot em-guest é não-confiável), nome
#           volta ao default e o raiznetd continua mostrando o MESMO device.
set -euo pipefail
source "$(dirname "$0")/lib.sh"
cd "$(dirname "$0")/.."

DATA_DIR=""
EMU_LOG=""
UI_PORT=8180
BASE="http://127.0.0.1:${UI_PORT}"
NIC_SUFFIX=",hostfwd=tcp:127.0.0.1:${UI_PORT}-:80"

cleanup() {
  emu_cleanup_default
  echo "webui-e2e: log do emulador em $EMU_LOG"
}
trap cleanup EXIT INT TERM

emu_ensure_qemu webui-e2e

export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e qemu >/dev/null
rm -f emu/flash.bin
emu/mkflash.sh >/dev/null
emu_build_raiznetd

DATA_DIR="$(mktemp -d /tmp/raiznetd-webui-e2e.XXXXXX)"
EMU_LOG="$(mktemp /tmp/emu-webui-e2e.XXXXXX.log)"

# ── Helpers ──────────────────────────────────────────────────────────────

fail() { echo "FALHOU: $1"; exit 1; }

# Todo curl contra a UI usa --max-time: se o guest travar, um curl sem
# timeout pendura a suíte inteira (visto na prática com o panic do /reset/wifi).
curl_ui() { curl --max-time 10 "$@"; }

wait_ui() {
  local deadline=$(( $(date +%s) + $1 ))
  while [ "$(date +%s)" -lt "$deadline" ]; do
    curl_ui -sf "${BASE}/api/status" >/dev/null 2>&1 && return 0
    sleep 2
  done
  return 1
}

device_id() {
  curl -sf "http://127.0.0.1:3000/v1/devices" | jq -r '.devices[0].id // empty'
}

reading_count() {
  curl -sf "http://127.0.0.1:3000/v1/devices/$1/telemetry" | jq '.readings | length'
}

# Espera até $2 segundos por reading_count($1) >= $3.
wait_count_ge() {
  local id=$1 timeout=$2 target=$3
  local deadline=$(( $(date +%s) + timeout ))
  local n=0
  while [ "$(date +%s)" -lt "$deadline" ]; do
    n=$(reading_count "$id" 2>/dev/null || echo 0)
    [ "$n" -ge "$target" ] && { echo "$n"; return 0; }
    sleep 2
  done
  echo "$n"
  return 1
}

# Espera até $2 segundos por uma leitura com ph.value == $3 no raiznetd.
wait_ph_value() {
  local id=$1 timeout=$2 want=$3
  local deadline=$(( $(date +%s) + timeout ))
  while [ "$(date +%s)" -lt "$deadline" ]; do
    if curl -sf "http://127.0.0.1:3000/v1/devices/$id/telemetry" \
        | jq -e --argjson w "$want" 'any(.readings[]?; (.ph.value // empty) == $w)' >/dev/null 2>&1; then
      return 0
    fi
    sleep 2
  done
  return 1
}

# check_get PATH EXPECTED_STATUS CT_PREFIX [LABEL]
check_get() {
  local path=$1 expected=$2 ct_prefix=$3 label="${4:-$1}"
  local out code ct
  out=$(curl_ui -s -o /dev/null -w "%{http_code} %{content_type}" "${BASE}${path}")
  code="${out%% *}"
  ct="${out#* }"
  [ "$code" = "$expected" ] || fail "$label: esperava status $expected, veio $code"
  if [ -n "$ct_prefix" ]; then
    case "$ct" in
      "$ct_prefix"*) ;;
      *) fail "$label: esperava content-type '$ct_prefix*', veio '$ct'" ;;
    esac
  fi
  echo "webui-e2e: OK $label -> $code $ct"
}

emu_start_raiznetd "$DATA_DIR" || { echo "FALHOU: raiznetd não subiu"; exit 1; }
emu_start_qemu emu/flash.bin "$EMU_LOG" "$NIC_SUFFIX"

echo "webui-e2e: aguardando UI local subir"
wait_ui 180 || fail "UI local (porta $UI_PORT) não respondeu após boot"

INITIAL_NAME="$(curl_ui -sf "${BASE}/api/status" | jq -r '.device_name // empty')"
[ -n "$INITIAL_NAME" ] || fail "device_name vazio em /api/status logo após boot"
case "$INITIAL_NAME" in
  Safrasense-aqua-*) ;;
  *) fail "device_name inicial não segue o padrão default (got: $INITIAL_NAME)" ;;
esac
echo "webui-e2e: device_name inicial = $INITIAL_NAME"

# ── Fase 1: páginas e assets estáticos + 404 ──────────────────────────────

echo "webui-e2e fase 1: páginas, assets estáticos e rota 404"
check_get "/"                     200 "text/html" "/"
check_get "/raiznet"              200 "text/html" "/raiznet"
check_get "/config"               200 "text/html" "/config"
check_get "/docs"                 200 "text/html" "/docs"
check_get "/reset/factory"        200 "text/html" "/reset/factory (GET, página de confirmação)"
check_get "/local.css"            200 "text/css"  "/local.css"
check_get "/dashboard.js"         200 ""           "/dashboard.js"
check_get "/local-nav.js"         200 ""           "/local-nav.js"
check_get "/this-route-does-not-exist" 404 ""      "rota inexistente"

# dashboard.js / local-nav.js: confere content-type real do handler
# (application/javascript), separado para dar erro mais claro se divergir.
DASH_CT="$(curl_ui -s -o /dev/null -w '%{content_type}' "${BASE}/dashboard.js")"
case "$DASH_CT" in
  application/javascript*) ;;
  *) fail "/dashboard.js: content-type inesperado '$DASH_CT'" ;;
esac
NAV_CT="$(curl_ui -s -o /dev/null -w '%{content_type}' "${BASE}/local-nav.js")"
case "$NAV_CT" in
  application/javascript*) ;;
  *) fail "/local-nav.js: content-type inesperado '$NAV_CT'" ;;
esac
echo "webui-e2e: fase 1 OK"

# ── Fase 2: /api/status ───────────────────────────────────────────────────

echo "webui-e2e fase 2: /api/status"
curl_ui -sf "${BASE}/api/status" | jq -e '
  (.device_name    | type=="string") and
  (.device_id      | type=="string") and
  (.mac            | type=="string") and
  (.ip             | type=="string") and
  (.wifi_ok        | type=="boolean") and
  (.server_ok      | type=="boolean") and
  (.buffer_pending | type=="number") and
  (.buffer_total   | type=="number") and
  (.sensors        | type=="object") and
  (.servers_external | type=="array") and
  (.servers_local    | type=="array")
' >/dev/null || fail "/api/status: shape do JSON inesperado"
echo "webui-e2e: fase 2 OK"

# ── Fase 3: /api/telemetry (valores sintéticos do QEMU) ───────────────────

echo "webui-e2e fase 3: /api/telemetry (aguardando primeira leitura local)"
DEADLINE=$(( $(date +%s) + 40 ))
TELEMETRY_OK=0
while [ "$(date +%s)" -lt "$DEADLINE" ]; do
  if curl_ui -sf "${BASE}/api/telemetry" >/tmp/webui-e2e-telemetry.json 2>/dev/null; then
    TELEMETRY_OK=1
    break
  fi
  sleep 2
done
[ "$TELEMETRY_OK" = 1 ] || fail "/api/telemetry nunca respondeu 200 (sem leitura local)"

jq -e '
  (.device_id | type=="string") and
  ((.readings.temp_ambient - 24.5 | fabs) < 0.05) and
  ((.readings.humidity     - 61.2 | fabs) < 0.05) and
  ((.readings.ec           - 1412 | fabs) < 0.5)
' /tmp/webui-e2e-telemetry.json >/dev/null || fail "/api/telemetry: valores sintéticos não batem (got: $(cat /tmp/webui-e2e-telemetry.json))"
rm -f /tmp/webui-e2e-telemetry.json
echo "webui-e2e: fase 3 OK"

# ── Fase 4: /api/force-read ────────────────────────────────────────────────

echo "webui-e2e fase 4: /api/force-read"
DEVICE_ID="$(device_id)"
[ -n "$DEVICE_ID" ] || fail "device_id vazio no raiznetd antes do force-read"
COUNT_BEFORE_FORCE="$(reading_count "$DEVICE_ID")"

FORCE_RESP="$(curl_ui -sf -X POST "${BASE}/api/force-read")"
[ "$(echo "$FORCE_RESP" | jq -r '.ok')" = "true" ] || fail "/api/force-read não retornou {\"ok\":true} (got: $FORCE_RESP)"

if ! COUNT_AFTER_FORCE="$(wait_count_ge "$DEVICE_ID" 10 $(( COUNT_BEFORE_FORCE + 1 )))"; then
  fail "force-read não gerou nova leitura no raiznetd em <10s (antes=$COUNT_BEFORE_FORCE depois=$COUNT_AFTER_FORCE)"
fi
echo "webui-e2e: fase 4 OK (leituras $COUNT_BEFORE_FORCE -> $COUNT_AFTER_FORCE)"

# ── Fase 5: /api/ph/manual ─────────────────────────────────────────────────

echo "webui-e2e fase 5: /api/ph/manual"

INVALID_CODE="$(curl_ui -s -o /tmp/webui-e2e-ph-invalid.json -w '%{http_code}' -X POST "${BASE}/api/ph/manual" --data-urlencode "ph=99")"
[ "$INVALID_CODE" = "400" ] || fail "/api/ph/manual com ph=99 esperava 400, veio $INVALID_CODE (body: $(cat /tmp/webui-e2e-ph-invalid.json))"
rm -f /tmp/webui-e2e-ph-invalid.json
echo "webui-e2e: ph=99 (inválido) -> 400 OK"

VALID_RESP="$(curl_ui -sf -X POST "${BASE}/api/ph/manual" --data-urlencode "ph=6.5")"
[ "$(echo "$VALID_RESP" | jq -r '.ok')" = "true" ] || fail "/api/ph/manual com ph=6.5 não retornou {\"ok\":true} (got: $VALID_RESP)"

wait_ph_value "$DEVICE_ID" 25 6.5 || fail "ph=6.5 não apareceu na próxima leitura do raiznetd em <25s"
echo "webui-e2e: fase 5 OK (ph=6.5 confirmado no raiznetd)"

# ── Fase 6: /config/save ───────────────────────────────────────────────────

echo "webui-e2e fase 6: /config/save"
curl_ui -sf -X POST "${BASE}/config/save" \
  --data-urlencode "device_name=WebuiE2E" \
  --data-urlencode "ext_count=1" \
  --data-urlencode "ext_name_0=raiznetd-public" \
  --data-urlencode "ext_url_0=http://10.0.2.2:3000/v1/telemetry" \
  --data-urlencode "loc_count=1" \
  --data-urlencode "loc_name_0=raiznetd-local" \
  --data-urlencode "loc_url_0=10.0.2.2:3001" \
  -o /dev/null || fail "/config/save não respondeu"

sleep 2
NAME_AFTER_SAVE="$(curl_ui -sf "${BASE}/api/status" | jq -r '.device_name // empty')"
[ "$NAME_AFTER_SAVE" = "WebuiE2E" ] || fail "nome não mudou após /config/save (got: $NAME_AFTER_SAVE)"
echo "webui-e2e: fase 6 OK (device_name = $NAME_AFTER_SAVE)"

# ── Fase 7: /reset/wifi ─────────────────────────────────────────────────────

echo "webui-e2e fase 7: /reset/wifi"
check_get "/reset/wifi" 200 "text/html" "/reset/wifi"
# Sob QEMU o firmware pula as chamadas WiFi.disconnect()/WiFi.begin() (guard
# QEMU_EMULATOR — inicializar o driver Wi-Fi sem rádio dá panic). O assert
# aqui é justamente que a UI continua respondendo após a rota.
wait_ui 30 || fail "UI local não respondeu logo após /reset/wifi"
echo "webui-e2e: fase 7 OK"

# ── Fase 8 (LAST): /reset/factory/confirm ──────────────────────────────────

echo "webui-e2e fase 8: /reset/factory/confirm (por último, apaga config+identidade)"
N_DEVICES_BEFORE="$(curl -sf "http://127.0.0.1:3000/v1/devices" | jq '.devices | length')"

check_get "/reset/factory" 200 "text/html" "/reset/factory (antes do confirm)"

curl_ui -sf -X POST "${BASE}/reset/factory/confirm" -o /dev/null || fail "/reset/factory/confirm não respondeu"

# O main loop apaga config+identidade e chama ESP.restart(). O reboot
# em-guest (SW_CPU_RESET) não é confiável no QEMU — o segundo boot pode
# pendurar — então espera o device iniciar o reset (UI parar de responder)
# e recria o processo do QEMU com a MESMA flash (equivalente a power-cycle;
# o erase acontece ANTES do restart, então já está persistido).
echo "webui-e2e: aguardando o device iniciar o reset"
RESET_STARTED=0
for _ in $(seq 1 20); do
  curl -sf --max-time 2 "${BASE}/api/status" >/dev/null 2>&1 || { RESET_STARTED=1; break; }
  sleep 1
done
[ "$RESET_STARTED" = 1 ] || fail "UI continuou respondendo 20s após /reset/factory/confirm (reset não executou)"

echo "webui-e2e: power-cycle do QEMU com a mesma flash"
emu_stop_qemu
emu_start_qemu emu/flash.bin "$EMU_LOG" "$NIC_SUFFIX"
wait_ui 180 || fail "UI local não voltou após /reset/factory/confirm (reboot)"

NAME_AFTER_RESET="$(curl_ui -sf "${BASE}/api/status" | jq -r '.device_name // empty')"
case "$NAME_AFTER_RESET" in
  Safrasense-aqua-*) ;;
  *) fail "device_name não voltou ao default após factory reset (got: $NAME_AFTER_RESET)" ;;
esac
[ "$NAME_AFTER_RESET" = "$INITIAL_NAME" ] || fail "device_name após factory reset difere do inicial (inicial=$INITIAL_NAME depois=$NAME_AFTER_RESET)"

ID_AFTER_RESET="$(device_id)"
[ "$ID_AFTER_RESET" = "$DEVICE_ID" ] || fail "device_id mudou após factory reset ($DEVICE_ID -> $ID_AFTER_RESET) — identidade deveria ser fixa no emulador"

N_DEVICES_AFTER="$(curl -sf "http://127.0.0.1:3000/v1/devices" | jq '.devices | length')"
[ "$N_DEVICES_AFTER" -eq "$N_DEVICES_BEFORE" ] || fail "raiznetd esperava $N_DEVICES_BEFORE device(s), achou $N_DEVICES_AFTER após factory reset"

echo "webui-e2e: fase 8 OK (nome voltou a '$NAME_AFTER_RESET', device_id inalterado, $N_DEVICES_AFTER device(s) no raiznetd)"

echo "webui-e2e OK"
