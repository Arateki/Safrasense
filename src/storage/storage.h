#pragma once
#include <Arduino.h>
#include <vector>

// Name + URL pair. Empty name means the entry is ignored.
struct ServerEntry {
  String name;
  String url;
};

struct DeviceConfig {
  String device_name;
  std::vector<ServerEntry> servers_external;  // internet servers
  std::vector<ServerEntry> servers_local;     // LAN servers
};

DeviceConfig loadConfig();
void         saveConfig(const DeviceConfig& cfg);
void         eraseConfig();
