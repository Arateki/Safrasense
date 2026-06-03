#include "storage.h"
#include "config.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WiFi.h>

// Serializes the whole config as JSON and stores it in a single NVS field.
// This allows fields to be added or removed without a manual migration.

DeviceConfig loadConfig() {
  Preferences p;
  p.begin(NVS_CONFIG_NS, true);  // true = read-only
  String json = p.getString("json", "");
  p.end();

  DeviceConfig cfg;

  if (json.isEmpty()) {
    // First boot: build defaults with the MAC suffix.
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    cfg.device_name = "Safrasense-aqua-" + mac.substring(mac.length() - 4);
    cfg.servers_external.push_back({ DEFAULT_SERVER_EXT_NAME, DEFAULT_SERVER_EXT_URL });
    return cfg;
  }

  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    Serial.println("[storage] Config corrompida, usando defaults.");
    cfg.device_name = "Safrasense-aqua";
    return cfg;
  }

  cfg.device_name = doc["name"] | "Safrasense-aqua";

  for (JsonObject s : doc["ext"].as<JsonArray>()) {
    String name = s["name"] | "";
    String url  = s["url"]  | "";
    if (name.length() > 0 && url.length() > 0) {
      cfg.servers_external.push_back({ name, url });
    }
  }
  for (JsonObject s : doc["local"].as<JsonArray>()) {
    String name = s["name"] | "";
    String url  = s["url"]  | "";
    if (name.length() > 0 && url.length() > 0) {
      cfg.servers_local.push_back({ name, url });
    }
  }

  return cfg;
}

void saveConfig(const DeviceConfig& cfg) {
  JsonDocument doc;
  doc["name"] = cfg.device_name;

  JsonArray ext = doc["ext"].to<JsonArray>();
  for (const auto& s : cfg.servers_external) {
    JsonObject o = ext.add<JsonObject>();
    o["name"] = s.name;
    o["url"]  = s.url;
  }

  JsonArray loc = doc["local"].to<JsonArray>();
  for (const auto& s : cfg.servers_local) {
    JsonObject o = loc.add<JsonObject>();
    o["name"] = s.name;
    o["url"]  = s.url;
  }

  String json;
  serializeJson(doc, json);

  Preferences p;
  p.begin(NVS_CONFIG_NS, false);  // false = read/write
  p.putString("json", json);
  p.end();
}

void eraseConfig() {
  Preferences p;
  p.begin(NVS_CONFIG_NS, false);
  p.clear();
  p.end();

  // Clear Wi-Fi credentials saved by the ESP32 hardware and WiFiManager.
  // The second 'true' parameter erases the SSID/Password from flash.
  WiFi.disconnect(true, true);
}
