#include "sensors.h"
#include "config.h"
#include <DHT.h>
#include <Adafruit_VL53L0X.h>
#include <Wire.h>
#include <math.h>

// Instances stay inside this module and are invisible outside.
// In Node.js this would be equivalent to top-level module variables without export.
static DHT              dht(PIN_DHT, DHT22);
static Adafruit_VL53L0X lox;
static bool             laserReady = false;

void initSensors() {
  pinMode(PIN_POWER_TDS, OUTPUT);
  pinMode(PIN_POWER_DHT, OUTPUT);
  digitalWrite(PIN_POWER_TDS, LOW);
  digitalWrite(PIN_POWER_DHT, LOW);

  pinMode(PIN_TDS,     INPUT);
  pinMode(PIN_BATTERY, INPUT);

  dht.begin();

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.beginTransmission(VL53_I2C_ADDR);
  if (Wire.endTransmission() == 0) {
    laserReady = lox.begin();
  }
  if (!laserReady) {
    Serial.println("[sensors] VL53L0X não encontrado — continuando sem ele.");
  }
}

static int batteryPercent(float v) {
  // Arduino map() performs linear interpolation between two intervals.
  // Lithium battery: 3.2V = 0%, 4.2V = 100%.
  int p = map((int)(v * 100), 320, 420, 0, 100);
  return constrain(p, 0, 100);
}

SensorData readSensors() {
  SensorData d;
  d.captured_at = millis();

  // Power up sensors that have a power control pin.
  digitalWrite(PIN_POWER_TDS, HIGH);
  digitalWrite(PIN_POWER_DHT, HIGH);
  delay(2000);  // DHT22 needs stabilization time

  // ── Battery ─────────────────────────────────────────────────────────────
  int rawBat  = analogRead(PIN_BATTERY);
  d.bat_volts  = (rawBat / 4095.0f) * 3.3f * 2.27f;
  d.bat_percent = batteryPercent(d.bat_volts);
  d.status.battery_ok = true;

  // ── VL53L0X laser (water level) ─────────────────────────────────────────
  if (laserReady) {
    VL53L0X_RangingMeasurementData_t m;
    lox.rangingTest(&m, false);
    d.status.laser_ok = (m.RangeStatus != 4);
    if (d.status.laser_ok) {
      d.water_level = m.RangeMilliMeter;
    }
  }

  // ── DHT22 (air temperature and humidity) ────────────────────────────────
  d.humidity     = dht.readHumidity();
  d.temp_ambient = dht.readTemperature();
  d.status.dht_ok = !isnan(d.temp_ambient) && !isnan(d.humidity);
  if (!d.status.dht_ok) {
    Serial.println("[sensors] DHT22 falhou na leitura.");
  }

  // ── TDS -> EC (nutrients) ───────────────────────────────────────────────
  float tempRef = d.status.dht_ok ? d.temp_ambient : 25.0f;
  int   rawTds  = analogRead(PIN_TDS);
  float vTds    = rawTds * (3.3f / 4095.0f);
  float comp    = vTds / (1.0f + 0.02f * (tempRef - 25.0f));
  float tds     = (133.42f * powf(comp, 3) - 255.86f * powf(comp, 2) + 857.39f * comp) * 0.5f;
  d.status.tds_ok = (tds >= 0);
  d.ec = d.status.tds_ok ? tds : NAN;

  // Power down to save energy.
  digitalWrite(PIN_POWER_TDS, LOW);
  digitalWrite(PIN_POWER_DHT, LOW);

  return d;
}

bool anySensorFailed(const SensorData& d) {
  return !d.status.dht_ok || !d.status.tds_ok || !d.status.laser_ok;
}
