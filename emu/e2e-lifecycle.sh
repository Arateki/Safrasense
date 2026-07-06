#!/usr/bin/env bash
# Teste e2e de ciclo de vida: reboot com flash preservada, queda do servidor
# raiznetd (buffer + reentrega) e force-read via UI local do firmware.
#   Fase 1: banco/flash limpos → device registra e telemetria chega.
#   Fase 2: muda o nome do device via /config/save (UI local, porta 8180).
#   Fase 3: reboot (emu_stop_qemu + emu_start_qemu na MESMA flash) → mesmo
#           device_id, seq continua (não reinicia), config sobrevive.
#   Fase 4: queda do raiznetd (~35s) → religa com o MESMO data dir → buffer
#           acumulado é entregue de uma vez e a telemetria continua.
#   Fase 5: force-read via /api/force-read → nova leitura em <10s.
set -euo pipefail
source "$(dirname "$0")/lib.sh"
cd "$(dirname "$0")/.."

DATA_DIR=""
EMU_LOG=""
UI_PORT=8180
NIC_SUFFIX=",hostfwd=tcp:127.0.0.1:${UI_PORT}-:80"

cleanup() {
  emu_cleanup_default
  echo "e2e-lifecycle: log do emulador em $EMU_LOG"
}
trap cleanup EXIT INT TERM

emu_ensure_qemu e2e-lifecycle

export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e qemu >/dev/null
rm -f emu/flash.bin
emu/mkflash.sh >/dev/null
emu_build_raiznetd

DATA_DIR="$(mktemp -d /tmp/raiznetd-e2e-lifecycle.XXXXXX)"
EMU_LOG="$(mktemp /tmp/emu-e2e-lifecycle.XXXXXX.log)"

# Espera até $3 segundos por >= $2 leituras no listener da porta $1.
wait_readings() {
  local port=$1 min=$2 deadline=$(( $(date +%s) + $3 ))
  while [ "$(date +%s)" -lt "$deadline" ]; do
    local id
    id=$(curl -sf "http://127.0.0.1:$port/v1/devices" | jq -r '.devices[0].id // empty' || true)
    if [ -n "$id" ]; then
      local n
      n=$(curl -sf "http://127.0.0.1:$port/v1/devices/$id/telemetry" | jq '.readings | length' || echo 0)
      [ "$n" -ge "$min" ] && return 0
    fi
    sleep 2
  done
  return 1
}

device_id() {
  curl -sf "http://127.0.0.1:3000/v1/devices" | jq -r '.devices[0].id // empty'
}

max_seq() {
  curl -sf "http://127.0.0.1:3000/v1/devices/$1/telemetry" | jq '[.readings[].seq] | max // 0'
}

reading_count() {
  curl -sf "http://127.0.0.1:3000/v1/devices/$1/telemetry" | jq '.readings | length'
}

wait_ui() {
  local deadline=$(( $(date +%s) + $1 ))
  while [ "$(date +%s)" -lt "$deadline" ]; do
    curl -sf "http://127.0.0.1:${UI_PORT}/api/status" >/dev/null 2>&1 && return 0
    sleep 2
  done
  return 1
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

emu_start_raiznetd "$DATA_DIR" || { echo "FALHOU: raiznetd não subiu"; exit 1; }
emu_start_qemu emu/flash.bin "$EMU_LOG" "$NIC_SUFFIX"

echo "e2e-lifecycle fase 1: registro + telemetria inicial"
wait_readings 3000 2 180 || { echo "FALHOU: sem >=2 leituras no servidor após boot inicial"; exit 1; }

DEVICE_ID="$(device_id)"
[ -n "$DEVICE_ID" ] || { echo "FALHOU: device_id vazio após fase 1"; exit 1; }
MAX_SEQ_1="$(max_seq "$DEVICE_ID")"
echo "e2e-lifecycle: device_id=$DEVICE_ID max_seq_1=$MAX_SEQ_1"

wait_ui 60 || { echo "FALHOU: UI local (porta $UI_PORT) não respondeu"; exit 1; }

echo "e2e-lifecycle fase 2: renomeia device via /config/save"
curl -sf -X POST "http://127.0.0.1:${UI_PORT}/config/save" \
  --data-urlencode "device_name=TesteReboot" \
  --data-urlencode "ext_count=1" \
  --data-urlencode "ext_name_0=raiznetd-public" \
  --data-urlencode "ext_url_0=http://10.0.2.2:3000/v1/telemetry" \
  --data-urlencode "loc_count=1" \
  --data-urlencode "loc_name_0=raiznetd-local" \
  --data-urlencode "loc_url_0=10.0.2.2:3001" \
  -o /dev/null || { echo "FALHOU: /config/save não respondeu"; exit 1; }

sleep 2
NAME_AFTER_SAVE="$(curl -sf "http://127.0.0.1:${UI_PORT}/api/status" | jq -r '.device_name // empty')"
[ "$NAME_AFTER_SAVE" = "TesteReboot" ] || { echo "FALHOU: nome não mudou após /config/save (got: $NAME_AFTER_SAVE)"; exit 1; }

echo "e2e-lifecycle fase 3: reboot (flash preservada)"
emu_stop_qemu
sleep 2
emu_start_qemu emu/flash.bin "$EMU_LOG" "$NIC_SUFFIX"

wait_ui 60 || { echo "FALHOU: UI local não voltou após reboot"; exit 1; }

MAX_SEQ_2="$MAX_SEQ_1"
DEADLINE=$(( $(date +%s) + 120 ))
while [ "$(date +%s)" -lt "$DEADLINE" ]; do
  MAX_SEQ_2="$(max_seq "$DEVICE_ID" 2>/dev/null || echo "$MAX_SEQ_1")"
  [ "$MAX_SEQ_2" -gt "$MAX_SEQ_1" ] && break
  sleep 2
done
[ "$MAX_SEQ_2" -gt "$MAX_SEQ_1" ] || { echo "FALHOU: seq não avançou após reboot (max_seq_1=$MAX_SEQ_1 max_seq_2=$MAX_SEQ_2)"; exit 1; }
echo "e2e-lifecycle: max_seq_2=$MAX_SEQ_2 (> max_seq_1=$MAX_SEQ_1, seq continuou)"

N_DEVICES="$(curl -sf "http://127.0.0.1:3000/v1/devices" | jq '.devices | length')"
[ "$N_DEVICES" -eq 1 ] || { echo "FALHOU: esperava 1 device após reboot, achou $N_DEVICES"; exit 1; }

ID_AFTER_REBOOT="$(device_id)"
[ "$ID_AFTER_REBOOT" = "$DEVICE_ID" ] || { echo "FALHOU: device_id mudou após reboot ($DEVICE_ID -> $ID_AFTER_REBOOT)"; exit 1; }

COUNT_AFTER_REBOOT="$(reading_count "$DEVICE_ID")"

NAME_AFTER_REBOOT="$(curl -sf "http://127.0.0.1:${UI_PORT}/api/status" | jq -r '.device_name // empty')"
[ "$NAME_AFTER_REBOOT" = "TesteReboot" ] || { echo "FALHOU: config não sobreviveu ao reboot (nome=$NAME_AFTER_REBOOT)"; exit 1; }
echo "e2e-lifecycle: 1 device, mesmo id, contagem=$COUNT_AFTER_REBOOT, nome='$NAME_AFTER_REBOOT' sobreviveu ao reboot"

echo "e2e-lifecycle fase 4: queda do servidor raiznetd"
emu_stop_raiznetd
echo "e2e-lifecycle: servidor caído, aguardando 35s (>=3 ciclos de telemetria de 10s)"
sleep 35
emu_start_raiznetd "$DATA_DIR" || { echo "FALHOU: raiznetd não voltou a subir com o mesmo data dir"; exit 1; }

TARGET=$(( COUNT_AFTER_REBOOT + 3 ))
if ! COUNT_AFTER_RESTART="$(wait_count_ge "$DEVICE_ID" 120 "$TARGET")"; then
  echo "FALHOU: buffer não foi entregue após queda do servidor (antes=$COUNT_AFTER_REBOOT depois=$COUNT_AFTER_RESTART esperado>=$TARGET)"
  exit 1
fi
echo "e2e-lifecycle: leituras saltaram de $COUNT_AFTER_REBOOT para $COUNT_AFTER_RESTART (buffer entregue)"

sleep 12
COUNT_GROWING="$(reading_count "$DEVICE_ID")"
[ "$COUNT_GROWING" -gt "$COUNT_AFTER_RESTART" ] || { echo "FALHOU: leituras pararam de crescer após reconexão (parado em $COUNT_GROWING)"; exit 1; }
echo "e2e-lifecycle: leituras continuam crescendo ($COUNT_AFTER_RESTART -> $COUNT_GROWING)"

echo "e2e-lifecycle fase 5: force-read"
COUNT_BEFORE_FORCE="$(reading_count "$DEVICE_ID")"
curl -sf -X POST "http://127.0.0.1:${UI_PORT}/api/force-read" -o /dev/null || { echo "FALHOU: /api/force-read não respondeu"; exit 1; }

if ! COUNT_AFTER_FORCE="$(wait_count_ge "$DEVICE_ID" 10 $(( COUNT_BEFORE_FORCE + 1 )) )"; then
  echo "FALHOU: force-read não gerou nova leitura em <10s (antes=$COUNT_BEFORE_FORCE depois=$COUNT_AFTER_FORCE)"
  exit 1
fi
echo "e2e-lifecycle: force-read entregou nova leitura ($COUNT_BEFORE_FORCE -> $COUNT_AFTER_FORCE) em <10s"

echo "e2e-lifecycle OK"
