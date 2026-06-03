#pragma once
#include <Arduino.h>
#include "storage/storage.h"
#include "identity/identity.h"
#include "sensors/sensors.h"

// Action requested by the web UI for main.cpp to execute in the loop.
enum PendingAction {
  ACTION_NONE,
  ACTION_FACTORY_RESET,  // clears config + identity and restarts
  ACTION_FORCE_READ,      // trigger immediate sensor read and send
};

// Starts the HTTP server on port 80.
void initHttpServer(DeviceConfig* cfg, const DeviceIdentity* id);

// Called on every loop() iteration to serve requests.
void handleHttpClients();

// Updates the latest reading displayed on the dashboard.
void updateLastReading(const SensorData& d);

// Returns the pending action and clears it (called once from main.cpp).
PendingAction getPendingAction();
