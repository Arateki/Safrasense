#pragma once
#include <Arduino.h>

// Which sensors are working. Each field is independent.
// The system operates normally with any combination of failures.
struct SensorStatus {
  bool dht_ok     = false;  // air temperature + humidity
  bool tds_ok     = false;  // electrical conductivity (nutrients)
  bool laser_ok   = false;  // distance / water level
  bool battery_ok = true;   // battery reading rarely fails
};

struct SensorData {
  float temp_ambient = NAN;  // deg C — NAN when DHT failed
  float humidity     = NAN;  // %     — NAN when DHT failed
  float ec           = NAN;  // ppm   — NAN when TDS failed
  float ph           = NAN;  // pH    — NAN when not provided
  float water_level  = -1;   // mm    — -1 when laser is offline/out of range
  float bat_volts    = 0;
  int   bat_percent  = 0;
  SensorStatus status;
  unsigned long captured_at = 0;  // millis() at read time
};

void       initSensors();
SensorData readSensors();
bool       anySensorFailed(const SensorData& d);
