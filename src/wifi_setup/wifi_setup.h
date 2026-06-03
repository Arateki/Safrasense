#pragma once
#include <Arduino.h>
#include "storage/storage.h"

// Connects through WiFiManager. Opens the configuration portal when needed.
// Updates cfg with the values the user entered in the portal.
void setupWifi(DeviceConfig& cfg);

// Reconnects without opening the portal. Uses already saved credentials.
void reconnectWifi();

String getMdnsName();
