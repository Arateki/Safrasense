#include "telemetry.h"
#include "buffer.h"
#include "config.h"
#include "../device/device.h"
#include <ArduinoJson.h>
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

static String toHex(const String& raw) {
  static const char* hex = "0123456789abcdef";
  String out;
  out.reserve(raw.length() * 2);
  for (size_t i = 0; i < raw.length(); i++) {
    uint8_t b = (uint8_t)raw[i];
    out += hex[b >> 4];
    out += hex[b & 0x0f];
  }
  return out;
}

static String fieldValue(float value, uint8_t decimals) {
  return String(roundf(value * powf(10, decimals)) / powf(10, decimals), (unsigned int)decimals);
}

static String u64ToString(uint64_t value) {
  char buf[21];
  snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
  return String(buf);
}

static String buildRaw(const TelemetryEntry& e) {
  String raw;
  raw.reserve(180);
  raw += gId->public_key_hex;
  raw += '|';
  raw += u64ToString(e.seq);
  raw += '|';
  raw += u64ToString(e.timestamp_ms);
  raw += "|0";
  if (!isnan(e.ec)) { raw += "|ec="; raw += fieldValue(e.ec, 0); }
  if (!isnan(e.ph)) { raw += "|ph="; raw += fieldValue(e.ph, 2); }
  if (e.water_level >= 0) { raw += "|waterLevel="; raw += fieldValue(e.water_level, 0); }
  if (!isnan(e.temp_ambient)) { raw += "|tempAmbient="; raw += fieldValue(e.temp_ambient, 2); }
  if (!isnan(e.humidity)) { raw += "|humidity="; raw += fieldValue(e.humidity, 2); }
  return raw;
}

static String buildJson(const TelemetryEntry& e, const String& raw, const String& sig) {
  JsonDocument doc;
  JsonArray blocks = doc["blocks"].to<JsonArray>();
  JsonObject block = blocks.add<JsonObject>();

  block["deviceId"]   = gId->public_key_hex;
  block["seq"]        = u64ToString(e.seq);
  block["timestamp"]  = u64ToString(e.timestamp_ms);
  block["keyVersion"] = 0;

  if (!isnan(e.ec)) block["ec"]["plain"] = roundf(e.ec);
  if (!isnan(e.ph)) block["ph"]["plain"] = roundf(e.ph * 100) / 100.0f;
  if (e.water_level >= 0) block["waterLevel"]["plain"] = e.water_level;
  if (!isnan(e.temp_ambient)) block["tempAmbient"]["plain"] = roundf(e.temp_ambient * 100) / 100.0f;
  if (!isnan(e.humidity)) block["humidity"]["plain"] = roundf(e.humidity * 100) / 100.0f;

  block["signature"] = sig;
  block["raw"]       = toHex(raw);

  String out;
  serializeJson(doc, out);
  return out;
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
  if (!gCfg || !gId || WiFi.status() != WL_CONNECTED) return;

  // First, make sure the basic registry is synchronized lazily.
  syncDeviceRegistry(*gCfg, *gId);

  uint32_t mask = targetMask();
  TelemetryEntry* e = bufferNextPending(mask);

  while (e != nullptr) {
    String raw  = buildRaw(*e);
    String sig  = signMessage(*gId, raw);
    String body = buildJson(*e, raw, sig);

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
