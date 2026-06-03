#pragma once
#include <Arduino.h>
#include "sensors/sensors.h"

// Server bitmask:
//   bit  0-15 → servers_external[0-15]
//   bit 16-31 → servers_local[0-15]
// "Sent" = (confirmed_mask & current_target_mask) == current_target_mask.
// Using the current mask (not the capture-time mask) allows:
//   - removing a server without leaving readings stuck waiting for it
//   - adding a server without sending historical data to it

// TODO deep sleep: add RTC_DATA_ATTR before the static variables in
// buffer.cpp so the buffer survives the sleep cycle.

struct TelemetryEntry {
  uint64_t seq;
  uint64_t timestamp_ms;
  float    temp_ambient;
  float    humidity;
  float    ec;
  float    ph;
  float    water_level;
  float    bat_volts;
  int8_t   bat_percent;
  uint32_t confirmed_mask;  // bits for servers that have already confirmed
};

void            bufferInit();
void            bufferAdd(const SensorData& d);
int             bufferPendingCount(uint32_t current_target_mask);
int             bufferTotal();

// Next entry that still has pending servers in the current mask.
TelemetryEntry* bufferNextPending(uint32_t current_target_mask);

// Marks a specific server as confirmed for a reading.
void            bufferConfirmServer(uint64_t seq, uint8_t server_bit);
