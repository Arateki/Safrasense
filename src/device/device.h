#pragma once
#include <Arduino.h>
#include "config.h"
#include "storage/storage.h"
#include "../identity/identity.h"

struct DeviceStatus {
  bool registered_ext;
  bool registered_local;
  String firmware_version;
  String model;
};

// Attempts to register the device on configured servers.
// Returns true when registration succeeded on every active server.
bool syncDeviceRegistry(const DeviceConfig& cfg, const DeviceIdentity& id);

// Resets registry status to force a new sync.
void invalidateDeviceRegistry();

DeviceStatus getDeviceStatus();
