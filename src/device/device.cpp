#include "device.h"
#include "net/net.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

static DeviceStatus gStatus = { false, false, "0.2.0", "Safrasense Aqua ESP32 v1" };

static String normalizedMac(const String& mac) {
  String out = mac;
  out.replace(":", "");
  out.toLowerCase();
  return out;
}

static bool postRegistration(const String& baseUrl, const DeviceConfig& cfg, const DeviceIdentity& id) {
  if (baseUrl.isEmpty() || !netReady()) return false;

  JsonDocument doc;
  doc["id"] = id.public_key_hex;
  doc["mac"] = normalizedMac(id.mac);
  doc["ownerPubkey"] = id.owner_public_key_hex;
  doc["name"] = cfg.device_name;
  doc["type"] = 0; // sensor_mains
  doc["publishTo"] = 2; // both

  JsonObject hw = doc["hardware"].to<JsonObject>();
  hw["model"] = gStatus.model;
  hw["firmware_version"] = gStatus.firmware_version;

  // Default privacy policy: everything public in the initial phase.
  JsonObject pp = doc["privacyPolicy"].to<JsonObject>();
  const char* fields[] = {"ph", "ec", "water_level", "temp_water", "temp_ambient", "humidity"};
  for (const char* f : fields) {
    pp[f]["default_disposition"] = 1; // PLAIN
  }

  String body;
  serializeJson(doc, body);

  String url = baseUrl;
  if (!url.endsWith("/")) url += "/";
  url += "v1/devices";

  HTTPClient http;
  http.setTimeout(8000);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  int code = http.POST(body);
  http.end();

  // 201 = created, 200 = OK, 409 = already exists (success for us).
  return (code == 201 || code == 200 || code == 409);
}

bool syncDeviceRegistry(const DeviceConfig& cfg, const DeviceIdentity& id) {
  bool allOk = true;

  // Register on external servers.
  if (!gStatus.registered_ext) {
    for (const auto& s : cfg.servers_external) {
      String baseUrl = s.url;
      baseUrl.replace("/v1/telemetry", "");
      if (!postRegistration(baseUrl, cfg, id)) allOk = false;
    }
    if (allOk && !cfg.servers_external.empty()) gStatus.registered_ext = true;
  }

  // Register on local servers.
  if (!gStatus.registered_local) {
    bool localOk = true;
    for (const auto& s : cfg.servers_local) {
      if (!postRegistration("http://" + s.url, cfg, id)) localOk = false;
    }
    if (localOk && !cfg.servers_local.empty()) gStatus.registered_local = true;
    else allOk = false;
  }

  return allOk;
}

void invalidateDeviceRegistry() {
  gStatus.registered_ext = false;
  gStatus.registered_local = false;
}

DeviceStatus getDeviceStatus() { return gStatus; }
