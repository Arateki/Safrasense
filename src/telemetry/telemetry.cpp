#include "telemetry.h"
#include "buffer.h"
#include "format.h"
#include "config.h"
#include "../device/device.h"
#include "../net/net.h"
#include <HTTPClient.h>
#include <WiFi.h>

static const DeviceConfig*   gCfg     = nullptr;
static const DeviceIdentity* gId      = nullptr;
static TelemetryState        gState;
static unsigned long         lastSendMs = 0;

void initTelemetry(const DeviceConfig* cfg, const DeviceIdentity* id) {
  gCfg = cfg;
  gId  = id;
  bufferInit();
}

static uint32_t targetMask() {
  if (!gCfg) return 0;
  uint32_t mask = 0;
  for (size_t i = 0; i < gCfg->servers_external.size() && i < 16; i++) {
    if (!gCfg->servers_external[i].url.isEmpty()) mask |= (1u << i);
  }
  for (size_t i = 0; i < gCfg->servers_local.size() && i < 16; i++) {
    if (!gCfg->servers_local[i].url.isEmpty()) mask |= (1u << (16 + i));
  }
  return mask;
}

static void setServerOnline(uint8_t bit, bool online) {
  if (bit >= 32) return;
  uint32_t flag = (1u << bit);
  if (online) gState.online_mask |= flag;
  else gState.online_mask &= ~flag;
}

static bool postTelemetry(const String& url, const String& body) {
  if (url.isEmpty()) return false;
  HTTPClient http;
  http.setTimeout(8000);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);

  if (code == 200) {
    http.end();
    return true;
  }

  // O servidor responde 207 com o erro por bloco no body — nunca 404 no
  // /v1/telemetry. Como enviamos 1 bloco por request, 207 = este bloco falhou.
  // "Device not found" é contrato dos dois servidores (TS e Rust): o servidor
  // não conhece este device (ex.: banco recriado). Invalida o registro local
  // para o próximo ciclo re-registrar e reenviar este mesmo bloco.
  if (code == 207) {
    String resp = http.getString();
    http.end();
    if (resp.indexOf("Device not found") >= 0) {
      Serial.printf("[telemetry] Servidor %s não conhece o device. Invalidando registro.\n", url.c_str());
      invalidateDeviceRegistry();
    } else {
      Serial.printf("[telemetry] Bloco rejeitado por %s: %s\n", url.c_str(), resp.c_str());
    }
    return false;
  }

  http.end();

  // Defesa: servidores antigos podem sinalizar device desconhecido com 404.
  if (code == 404) {
    Serial.printf("[telemetry] Servidor %s retornou 404. Invalidando registro.\n", url.c_str());
    invalidateDeviceRegistry();
  }

  return false;
}

void sendPending() {
  if (!gCfg || !gId || !netReady()) return;

  // First, make sure the basic registry is synchronized lazily.
  syncDeviceRegistry(*gCfg, *gId);

  uint32_t mask = targetMask();
  TelemetryEntry* e = bufferNextPending(mask);

  while (e != nullptr) {
    String raw  = telemetryBuildRaw(*e, gId->public_key_hex);
    String sig  = signMessage(*gId, raw);
    String body = telemetryBuildJson(*e, gId->public_key_hex, raw, sig);

    // External servers
    for (size_t i = 0; i < gCfg->servers_external.size() && i < 16; i++) {
      uint8_t bit = (uint8_t)i;
      if (e->confirmed_mask & (1u << bit)) continue;
      if (!(mask & (1u << bit))) continue;
      if (postTelemetry(gCfg->servers_external[i].url, body)) {
        setServerOnline(bit, true);
        bufferConfirmServer(e->seq, bit);
      } else {
        setServerOnline(bit, false);
      }
    }

    // Local servers
    for (size_t i = 0; i < gCfg->servers_local.size() && i < 16; i++) {
      uint8_t bit = (uint8_t)(16 + i);
      if (e->confirmed_mask & (1u << bit)) continue;
      if (!(mask & (1u << bit))) continue;
      String url = "http://" + gCfg->servers_local[i].url + "/v1/telemetry";
      if (postTelemetry(url, body)) {
        setServerOnline(bit, true);
        bufferConfirmServer(e->seq, bit);
      } else {
        setServerOnline(bit, false);
      }
    }

    bool allDone = (e->confirmed_mask & mask) == mask;
    if (allDone) {
      gState.last_send_ok = true;
      gState.fail_streak  = 0;
      lastSendMs          = millis();
    } else {
      gState.last_send_ok = false;
      gState.fail_streak++;
      break;
    }
    e = bufferNextPending(mask);
  }

  if (lastSendMs > 0) {
    unsigned long elapsed = (millis() - lastSendMs) / 1000;
    gState.last_send_time = elapsed < 60 ? String(elapsed) + "s atrás" : String(elapsed / 60) + "min atrás";
  }
}

TelemetryState getTelemetryState() { return gState; }
void clearTelemetryServerStatus() { gState.online_mask = 0; }
int pendingCount() { return bufferPendingCount(targetMask()); }
