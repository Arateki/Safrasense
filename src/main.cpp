#include <Arduino.h>
#include "config.h"
#include "sensors/sensors.h"
#include "leds/leds.h"
#include "buttons/buttons.h"
#include "storage/storage.h"
#include "identity/identity.h"
#include "telemetry/buffer.h"
#include "telemetry/telemetry.h"
#include "wifi_setup/wifi_setup.h"
#include "http_local/http_local.h"
#include "device/device.h"

SET_LOOP_TASK_STACK_SIZE(16384);

static DeviceConfig   cfg;
static DeviceIdentity identity;
static unsigned long  lastTelemetryMs = 0;

void setup() {
  Serial.begin(115200);

  initLeds();
  setLedState(LED_BOOTING);
  initButtons();
  initSensors();

  cfg      = loadConfig();
  identity = loadOrCreateIdentity();

  setLedState(LED_WIFI_CONNECTING);
  setupWifi(cfg);  // blocks until connected or the portal times out

  // Try to register the device right after Wi-Fi connects.
  syncDeviceRegistry(cfg, identity);

  initTelemetry(&cfg, &identity);
  initHttpServer(&cfg, &identity);

  setLedState(LED_NORMAL);
  lastTelemetryMs = millis();
  Serial.println("[main] Iniciado.");
}

void loop() {
  // ── 1. BOOT button ────────────────────────────────────────────────────
  ButtonEvent evt = tickButtons();

  if (evt == BTN_SHORT_PRESS) {
    // Reconnect Wi-Fi while keeping config and identity intact.
    setLedState(LED_RESET_SHORT);
    reconnectWifi();

  } else if (evt == BTN_LONG_PRESS) {
    // Config reset: clears Wi-Fi, servers, and name.
    // Does NOT erase the keypair; device_id stays the same on the network.
    setLedState(LED_RESET_LONG);
    delay(1000);
    eraseConfig();
    ESP.restart();
  }

  // ── 2. LEDs and HTTP requests ─────────────────────────────────────────
  tickLeds();
  handleHttpClients();

  // ── 3. Pending web UI action ─────────────────────────────────────────
  PendingAction pending = getPendingAction();
  if (pending == ACTION_FACTORY_RESET) {
    setLedState(LED_RESET_LONG);
    delay(1000);
    eraseConfig();
    eraseIdentity();  // the only place that erases the keypair
    ESP.restart();
  } else if (pending == ACTION_FORCE_READ) {
    lastTelemetryMs = millis() - TELEMETRY_INTERVAL_MS;
  }

  // ── 4. Telemetry cycle ────────────────────────────────────────────────
  unsigned long now = millis();
  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;

    SensorData reading = readSensors();
    bufferAdd(reading);
    updateLastReading(reading);

    sendPending();

    // Update LEDs according to the current state.
    TelemetryState ts     = getTelemetryState();
    bool sensorFail       = anySensorFailed(reading);
    bool serverOffline    = !ts.last_send_ok && ts.fail_streak > 0;

    if (sensorFail)         setLedState(LED_SENSOR_FAIL);
    else if (serverOffline) setLedState(LED_SERVER_OFFLINE);
    else                    setLedState(LED_NORMAL);

    if (ts.last_send_ok) blinkOnSend();
  }
}
