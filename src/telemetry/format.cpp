#include "format.h"
#include <ArduinoJson.h>

// MOVIDO de telemetry.cpp:39-100 — sem nenhuma mudanca de logica.
namespace {

String toHex(const String& raw) {
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

String fieldValue(float value, uint8_t decimals) {
  return String(roundf(value * powf(10, decimals)) / powf(10, decimals), (unsigned int)decimals);
}

String u64ToString(uint64_t value) {
  char buf[21];
  snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
  return String(buf);
}

}  // namespace

String telemetryBuildRaw(const TelemetryEntry& e, const String& devicePubkeyHex) {
  String raw;
  raw.reserve(180);
  raw += devicePubkeyHex;
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

String telemetryBuildJson(const TelemetryEntry& e, const String& devicePubkeyHex,
                           const String& raw, const String& sigHex) {
  JsonDocument doc;
  JsonArray blocks = doc["blocks"].to<JsonArray>();
  JsonObject block = blocks.add<JsonObject>();

  block["deviceId"]   = devicePubkeyHex;
  block["seq"]        = u64ToString(e.seq);
  block["timestamp"]  = u64ToString(e.timestamp_ms);
  block["keyVersion"] = 0;

  if (!isnan(e.ec)) block["ec"]["plain"] = roundf(e.ec);
  if (!isnan(e.ph)) block["ph"]["plain"] = roundf(e.ph * 100) / 100.0f;
  if (e.water_level >= 0) block["waterLevel"]["plain"] = e.water_level;
  if (!isnan(e.temp_ambient)) block["tempAmbient"]["plain"] = roundf(e.temp_ambient * 100) / 100.0f;
  if (!isnan(e.humidity)) block["humidity"]["plain"] = roundf(e.humidity * 100) / 100.0f;

  block["signature"] = sigHex;
  block["raw"]       = toHex(raw);

  String out;
  serializeJson(doc, out);
  return out;
}
